/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Compositor direct-access pause/resume and zero-copy shared Vulkan views.
 */


#include "CompositorPrivate.h"


//	#pragma mark - direct access (M11 step 5, decision 9)


/*!	A program is about to get B_DIRECT_START (a BDirectWindow) or has the
	screen (BWindowScreen). Called with fWindowLock write-locked, before the
	program learns the front buffer: waits for the compose in flight (a late
	fence is not a failure, decision 14), then every window draws to the
	screen again. The redraw the CPU-mode way follows from the compose
	thread once this write lock is released (_ApplyMode), because this can
	be called from inside a clipping rebuild.
*/
void
Compositor::DirectAccessStarting()
{
	if (!fActive || fPaused)
		return;
	{
		BAutolock slot(fComposeSlot);
		fPaused = true;
	}
	_RebindEngines(false);
	{
		BAutolock _(fLayerLock);
		fLayers->MakeEmpty();
	}
	_DropAnimations();
	log_line("compositor paused: direct window");

	fPauseRedraw = true;
	fModePending = true;
	release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
}


/*!	fWindowLock write-locked. When the last direct access stops, the resume
	is left to the compose thread: it takes the window lock after the one
	held now, so a direct window that is moved (stop, then start, under one
	write lock) never resumes and pauses the compositor.
*/
void
Compositor::DirectAccessChanged(Window* window, bool accessing)
{
	BAutolock _(fEntryLock);
	if (accessing) {
		if (!fDirectWindows.HasItem(window))
			fDirectWindows.AddItem(window);
		return;
	}
	if (fDirectWindows.RemoveItem(window) && fDirectWindows.IsEmpty()
		&& fPaused) {
		fModePending = true;
		release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
	}
}


//!	BWindowScreen: Desktop::LockDirectScreen() and UnlockDirectScreen().
void
Compositor::DirectScreenChanged(bool locked)
{
	// M11 fourth review: an unlock gives its count back below even when
	// the GPU mode has ended meanwhile.
	if (!fActive && locked)
		return;
	if (locked) {
		if (!fDesktop->LockAllWindows())
			return;
		// set before the pause, so a resume the pause schedules sees it
		atomic_add(&fDirectScreenWanted, 1);
		DirectAccessStarting();
		fDesktop->UnlockAllWindows();
		return;
	}
	// M11 review: a count, not a flag. A team that waits for the lock
	// while another gives it up keeps the pause.
	// Review after M11: atomic_add, not a compare-and-swap. Haiku's inline
	// atomic_test_and_set is a weak CAS and returns the expected value
	// also when it failed (ARM64), which would resume while a team still
	// holds the screen. A count that was already 0 (a lock taken while the
	// GPU mode was off) is given back.
	int32 wanted = atomic_add(&fDirectScreenWanted, -1);
	if (wanted <= 0) {
		atomic_add(&fDirectScreenWanted, 1);
		wanted = 0;
	}
	if (wanted > 1 || !fActive)
		return;
	fModePending = true;
	release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
}


/*!	The compose thread, outside the compose slot: after a pause, the desktop
	is redrawn the CPU-mode way; once no program has direct access, the
	windows go back to their buffers, every window draws once, and the
	compose starts again.
*/
void
Compositor::_ApplyMode()
{
	if (fSkia != NULL)
		fSkia->SetComposeBlocked(true);
	bool locked = fDesktop->LockAllWindows();
	if (fSkia != NULL)
		fSkia->SetComposeBlocked(false);
	if (!locked)
		return;
	fModePending = false;
	fEntryLock.Lock();
	bool direct = !fDirectWindows.IsEmpty()
		|| atomic_get(&fDirectScreenWanted) > 0;
	fEntryLock.Unlock();
	if (fActive && fPaused && !direct) {
		_RebindEngines(true);
		fPaused = false;
		fPauseRedraw = false;
		// the clipping is rebuilt with the GPU mode's regions, the buffers
		// are checked, and every window draws all of itself once
		fDesktop->CompositorTurnedOff();
		Damage(BRegion(BRect(0, 0, (float)fFrameWidth - 1,
			(float)fFrameHeight - 1)));
		log_line("compositor resumed");
	} else if (fPaused && fPauseRedraw) {
		fPauseRedraw = false;
		fDesktop->CompositorTurnedOff();
	}
	fDesktop->UnlockAllWindows();
}


//!	All windows locked: every window draws into its buffer, or to the screen.
void
Compositor::_RebindEngines(bool toBuffers)
{
	BAutolock _(fEntryLock);
	for (int32 i = 0; i < fEntries.CountItems(); i++) {
		Entry* entry = fEntries.ItemAt(i);
		if (entry->window == NULL || entry->window->GetDrawingEngine() == NULL)
			continue;
		if (toBuffers)
			entry->window->GetDrawingEngine()->SetHWInterface(entry->iface);
		else
			entry->window->GetDrawingEngine()->SetHWInterface(fScreen);
	}
	// M11 review: the resume ends the pause under the same lock, so a
	// window AddWindow binds in between is not left on the screen
	if (toBuffers)
		fPaused = false;
}


//!	No animation while paused (step 5) or after the switch to the CPU mode.
void
Compositor::_DropAnimations()
{
	BAutolock entryLock(fEntryLock);
	BAutolock _(fAnimLock);
	for (int32 i = 0; i < fEntries.CountItems(); i++)
		fEntries.ItemAt(i)->animOpen = false;
	fGhosts.MakeEmpty();
	fSlideActive = false;
	fAnimationsRunning = false;
}


/*!	The CPU mode, or the device goes: every shared image that app_server
	holds goes back to its program, so none waits in its acquire for an
	image that will never be shown. Its next present is refused and it makes
	a copy swapchain (docs/APP-SERVER-COMPOSITOR.md, "The reset path").
*/
void
Compositor::_ReturnSharedImages()
{
	BAutolock _(fShareLock);
	for (int32 i = 0; i < fShares.CountItems(); i++) {
		Share* share = fShares.ItemAt(i);
		share->broken = true;
		if (share->ended) {
			// M11 review: app_server ended it, the program still waits
			if (share->giveBack)
				_ShareGiveBack(share);
			continue;
		}
		for (uint32 n = 0; n < share->count; n++) {
			Share::Image& image = share->images[n];
			if ((int32)n == share->current || (int32)n == share->pending
				|| image.retiring) {
				image.retiring = false;
				release_sem_etc(image.sem, 1, B_DO_NOT_RESCHEDULE);
			}
		}
		share->current = -1;
		share->pending = -1;
	}
}



//	#pragma mark - shared views (M11 step 3b)


/*!	"app_server: window <id> zero-copy off: <reason>", once per window.
	Called with all windows locked.
*/
void
Compositor::_ShareLog(Window* window, const char* reason)
{
	BAutolock _(fEntryLock);
	Entry* entry = _FindEntry(window);
	if (entry == NULL || entry->shareLogged)
		return;
	entry->shareLogged = true;
	int32 id = window->ServerWindow() != NULL
		? window->ServerWindow()->ClientToken() : -1;
	log_line("window %" B_PRId32 " zero-copy off: %s", id, reason);
}


/*!	"app_server: window <id> zero-copy on", once per window, when its
	first swapchain is imported (M11 stick, the counterpart of _ShareLog).
	Takes fEntryLock, so it is never called with fShareLock held.
*/
void
Compositor::_ShareLogOn(Window* window)
{
	BAutolock _(fEntryLock);
	Entry* entry = _FindEntry(window);
	if (entry == NULL || entry->shareOnLogged)
		return;
	entry->shareOnLogged = true;
	int32 id = window->ServerWindow() != NULL
		? window->ServerWindow()->ClientToken() : -1;
	log_line("window %" B_PRId32 " zero-copy on", id);
}


/*!	The view's part of the screen and the screen position of its pixel
	(0, 0), from its window. Called with all windows and fShareLock locked.
	A view that is gone, or that is in another window now, has no shape.
*/
void
Compositor::_ShareShape(Share* share)
{
	share->shape.MakeEmpty();
	Window* window = share->window;
	// M11 review: ~ServerWindow clears App() before ~Window ends the share.
	// Read once (third review): it can be cleared between two reads.
	if (window == NULL || share->ended || window->ServerWindow() == NULL)
		return;
	ServerApp* app = window->ServerWindow()->App();
	if (app == NULL)
		return;
	View* view = NULL;
	if (app->ViewTokens().GetToken(share->viewToken,
			B_HANDLER_TOKEN, (void**)&view) != B_OK
		|| view == NULL || view->Window() != window || !view->IsVisible())
		return;
	BRegion content;
	window->GetContentRegion(&content);
	share->shape = view->ScreenAndUserClipping(&content);
	IntRect bounds = view->Bounds();
	IntPoint origin(bounds.left, bounds.top);
	view->LocalToScreenTransform().Apply(&origin);
	share->left = origin.x;
	share->top = origin.y;
}


/*!	Frees the imports of a share, with the device they were made on. The
	library's GEM_CLOSE ends each import's id. Called with fShareLock.
*/
void
Compositor::_ShareFree(Share* share)
{
	CompositorVulkan* vk = fVulkan;
	for (uint32 i = 0; i < share->count; i++) {
		Share::Image& image = share->images[i];
		if (vk != NULL && share->generation == fGeneration) {
			if (image.buffer != VK_NULL_HANDLE)
				vk->vkDestroyBuffer(vk->device, image.buffer, NULL);
			if (image.memory != VK_NULL_HANDLE)
				vk->vkFreeMemory(vk->device, image.memory, NULL);
		}
		image.buffer = VK_NULL_HANDLE;
		image.memory = VK_NULL_HANDLE;
	}
	share->count = 0;
}


/*!	M11 review: gives every image app_server holds of this share back to
	the program (current, pending, overtaken), as _ReturnSharedImages does
	for all. Compose thread at the start of a frame, with fShareLock: the
	frame before waited for its fence, so no GPU work reads them.
*/
void
Compositor::_ShareGiveBack(Share* share)
{
	share->giveBack = false;
	for (uint32 n = 0; n < share->count; n++) {
		Share::Image& image = share->images[n];
		if ((int32)n == share->current || (int32)n == share->pending
			|| image.retiring) {
			image.retiring = false;
			release_sem_etc(image.sem, 1, B_DO_NOT_RESCHEDULE);
		}
	}
	share->current = -1;
	share->pending = -1;
}


//!	Frees the shares that ended. Compose thread, between frames.
void
Compositor::_ShareRetire()
{
	for (int32 i = fShares.CountItems(); i-- > 0;) {
		Share* share = fShares.ItemAt(i);
		if (!share->ended)
			continue;
		if (share->giveBack)
			_ShareGiveBack(share);
		_ShareFree(share);
		fShares.RemoveItemAt(i);
		delete share;
	}
}


/*!	Whether the ring has passed an image's present mark (from
	r5_drm_mark_get). FENCE_WAIT with timeout 0: the compose never waits
	for a program.
*/
bool
Compositor::_ShareReady(uint64 sequence)
{
	// 0: no submit was ever marked on this image (a new buffer, or a mark
	// set before the file's first submit). It is not shown; the WSI marks
	// after the copy into the image, so its mark is never 0.
	if (sequence == 0)
		return false;
	r5_gpu_fence wait;
	memset(&wait, 0, sizeof(wait));
	wait.size = sizeof(wait);
	wait.version = 1;
	wait.sequence = sequence;
	wait.timeout_us = 0;
	return fFenceFd >= 0
		&& ioctl(fFenceFd, R5_GPU_FENCE_WAIT, &wait, sizeof(wait)) == 0;
}


/*!	Gives back each overtaken image whose present mark the ring has
	passed: until then the copy into it may still run (review, M11 step
	3b). Compose thread; sets fShareRetiring while one still waits.
*/
void
Compositor::_ShareRetireDone()
{
	if (!fShareRetiring)
		return;
	BAutolock _(fShareLock);
	bool waiting = false;
	for (int32 i = 0; i < fShares.CountItems(); i++) {
		Share* share = fShares.ItemAt(i);
		for (uint32 n = 0; n < share->count; n++) {
			Share::Image& image = share->images[n];
			if (!image.retiring)
				continue;
			uint64 sequence = 0;
			bool gone = share->ended || share->broken || fVulkan == NULL
				|| share->generation != fGeneration
				|| fVulkan->markGet(image.importId, &sequence) != 0;
			if (gone) {
				// nothing reads it any more; an ended share's program
				// has stopped waiting on it. M11 review: not when
				// app_server ended it (giveBack): that program still waits.
				// Fourth review: so does the program of a share that did
				// not end (broken, a restart, a failed mark read).
				image.retiring = false;
				if (share->giveBack || !share->ended)
					release_sem_etc(image.sem, 1, B_DO_NOT_RESCHEDULE);
				continue;
			}
			if (!_ShareReady(sequence)) {
				waiting = true;
				continue;
			}
			image.retiring = false;
			release_sem_etc(image.sem, 1, B_DO_NOT_RESCHEDULE);
		}
	}
	fShareRetiring = waiting;
}


/*!	The start of a frame: frees what ended, shows each pending image whose
	present mark the ring has passed, gives the image it replaces back to
	the program, and copies what this frame draws. A pending image that is
	not ready yet keeps the previous one; its view goes into retry.
*/
void
Compositor::_ShareFrame(ShareDrawList& draws, BRegion& retry)
{
	BAutolock _(fShareLock);
	_ShareRetire();
	for (int32 i = 0; i < fShares.CountItems(); i++) {
		Share* share = fShares.ItemAt(i);
		if (share->broken || share->generation != fGeneration)
			continue;
		if (share->pending >= 0) {
			Share::Image& next = share->images[share->pending];
			uint64 sequence = 0;
			// M11 review: mark 0 never passes (the WSI marks after its
			// copy, so it is never 0); retrying it would compose every
			// refresh period for good. Such a share is broken.
			if (fVulkan->markGet(next.importId, &sequence) != 0
				|| sequence == 0) {
				share->broken = true;
				// M11 review: the program still waits on these
				_ShareGiveBack(share);
				continue;
			}
			if (_ShareReady(sequence)) {
				int32 old = share->current;
				share->current = share->pending;
				share->pending = -1;
				// the last frame that read it is done (its fence)
				if (old >= 0)
					release_sem_etc(share->images[old].sem, 1,
						B_DO_NOT_RESCHEDULE);
			} else
				retry.Include(&share->shape);
		}
		if (share->current < 0 || share->shape.CountRects() == 0)
			continue;
		Share::Image& image = share->images[share->current];
		ShareDraw* draw = new(std::nothrow) ShareDraw;
		if (draw == NULL)
			continue;
		draw->window = share->window;
		draw->buffer = image.buffer;
		draw->width = image.width;
		draw->height = image.height;
		draw->pitch = image.pitch;
		draw->shape = share->shape;
		draw->left = share->left;
		draw->top = share->top;
		if (!draws.AddItem(draw))
			delete draw;
	}
}


void
Compositor::ShareDeclined(Window* window)
{
	_ShareLog(window, "lavapipe");
}


/*!	Imports a Vulkan view's swapchain images. app_server believes the
	kernel: each image's real size (BO_IMPORT, through the library) must
	hold its pitch times its height, and its semaphore must belong to the
	connection's team. Any refusal refuses the whole swapchain, and the
	program keeps the copy path.
*/
status_t
Compositor::ShareImages(Window* window, int32 viewToken, team_id team,
	uint32 count, const compositor_share_image* images, int32* _shareId)
{
	if (!fActive)
		return B_NOT_ALLOWED;
	if (fOnLavapipe) {
		_ShareLog(window, "lavapipe");
		return B_NOT_ALLOWED;
	}
	if (atomic_get(&fZeroCopyOff) != 0) {
		_ShareLog(window, "setting");
		return B_NOT_ALLOWED;
	}
	CompositorVulkan* vk = fVulkan;
	if (vk == NULL || !vk->canImport || fFenceFd < 0) {
		_ShareLog(window, "no import");
		return B_NOT_ALLOWED;
	}
	if (count == 0 || count > kMaxShareImages)
		return B_BAD_VALUE;

	for (uint32 i = 0; i < count; i++) {
		sem_info info;
		if (get_sem_info(images[i].sem, &info) != B_OK
			|| !r5_share_sem_ok(info.team, team)) {
			_ShareLog(window, "sem");
			return B_NOT_ALLOWED;
		}
		if (images[i].format != (uint32)B_RGBA32
			&& images[i].format != (uint32)B_RGB32) {
			_ShareLog(window, "format");
			return B_BAD_VALUE;
		}
		if (images[i].pitch % 4 != 0
			|| !r5_share_size_ok(images[i].width, images[i].height,
				images[i].pitch, R5_SHARE_MAX_IMAGE_BYTES)) {
			_ShareLog(window, "size");
			return B_BAD_VALUE;
		}
	}

	Share* share = new(std::nothrow) Share;
	if (share == NULL)
		return B_NO_MEMORY;
	share->window = window;
	share->viewToken = viewToken;
	share->team = team;
	share->count = 0;
	share->generation = fGeneration;
	share->current = -1;
	share->pending = -1;
	share->ended = false;
	share->broken = false;
	share->giveBack = false;
	share->left = 0;
	share->top = 0;

	const char* reason = NULL;
	for (uint32 i = 0; i < count && reason == NULL; i++) {
		const compositor_share_image& in = images[i];
		Share::Image& image = share->images[i];
		memset(&image, 0, sizeof(image));
		share->count = i + 1;
		image.sem = in.sem;
		image.width = in.width;
		image.height = in.height;
		image.pitch = in.pitch;

		VkExternalMemoryBufferCreateInfo external = {};
		external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
		external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.pNext = &external;
		bufferInfo.size = (uint64)in.pitch * in.height;
		bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vk->vkCreateBuffer(vk->device, &bufferInfo, NULL, &image.buffer)
				!= VK_SUCCESS) {
			image.buffer = VK_NULL_HANDLE;
			reason = "import";
			break;
		}
		VkMemoryRequirements requirements;
		vk->vkGetBufferMemoryRequirements(vk->device, image.buffer,
			&requirements);

		int fd = -1;
		if (vk->tokenToFd(-1, in.token, team, &fd, &image.importId) != 0) {
			reason = "import";
			break;
		}
		// RADV takes the fd on success; on a failure it stays ours.
		VkImportMemoryFdInfoKHR import = {};
		import.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
		import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
		import.fd = fd;
		VkMemoryAllocateInfo allocateInfo = {};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.pNext = &import;
		allocateInfo.allocationSize = requirements.size;
		allocateInfo.memoryTypeIndex = vk->memoryType;
		if ((requirements.memoryTypeBits & (1u << vk->memoryType)) == 0
			|| vk->vkAllocateMemory(vk->device, &allocateInfo, NULL,
				&image.memory) != VK_SUCCESS) {
			image.memory = VK_NULL_HANDLE;
			close(fd);
			reason = "import";
			break;
		}
		// The kernel's size, not the program's: pitch times height, and
		// the buffer RADV binds, must fit in it.
		uint64 bytes = 0;
		if (vk->importSize(image.importId, &bytes) != 0
			|| !r5_share_size_ok(in.width, in.height, in.pitch, bytes)
			|| requirements.size > bytes) {
			reason = "size";
			break;
		}
		if (vk->vkBindBufferMemory(vk->device, image.buffer, image.memory, 0)
				!= VK_SUCCESS) {
			reason = "import";
			break;
		}
	}

	if (reason != NULL) {
		// Not in the list yet, so no lock: and _ShareLog takes fEntryLock,
		// which comes before fShareLock (RemoveWindow).
		_ShareFree(share);
		delete share;
		_ShareLog(window, reason);
		return B_NOT_ALLOWED;
	}
	{
		BAutolock _(fShareLock);
		// A view has one swapchain: an older one of the same view ends.
		// Fourth review: the compose thread is woken to free it, as for
		// ShareEnd, or it waits for the next damage.
		bool endedOld = false;
		for (int32 i = 0; i < fShares.CountItems(); i++) {
			Share* old = fShares.ItemAt(i);
			if (old->team == team && old->viewToken == viewToken
				&& !old->ended) {
				old->ended = true;
				old->giveBack = true;
				endedOld = true;
			}
		}
		if (endedOld)
			release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
		share->id = ++fShareCounter;
		if (!fShares.AddItem(share)) {
			_ShareFree(share);
			delete share;
			return B_NO_MEMORY;
		}
		_ShareShape(share);
		*_shareId = share->id;
	}
	// After fShareLock: _ShareLogOn takes fEntryLock, which comes first.
	_ShareLogOn(window);
	return B_OK;
}


/*!	A present: the image becomes pending. An image that was pending and
	was never shown goes back to the program at once. The compose shows
	the new one once its mark has passed.
*/
status_t
Compositor::SharePresent(team_id team, int32 viewToken, int32 shareId,
	uint32 index)
{
	BRegion damage;
	{
		BAutolock _(fShareLock);
		Share* share = NULL;
		for (int32 i = 0; i < fShares.CountItems(); i++) {
			Share* scan = fShares.ItemAt(i);
			if (scan->id == shareId && scan->team == team
				&& scan->viewToken == viewToken) {
				share = scan;
				break;
			}
		}
		// B_BAD_VALUE: not the view's swapchain any more (a newer one
		// replaced it); the program's new swapchain still shares.
		// B_NOT_ALLOWED: this view takes no shared images now (a lost
		// import, a reset, the CPU mode); the program takes the copy path.
		if (share == NULL || share->ended || index >= share->count)
			return B_BAD_VALUE;
		if (share->broken || !fActive)
			return B_NOT_ALLOWED;
		if ((int32)index != share->current && (int32)index != share->pending) {
			// The overtaken image is never shown, but the copy into it
			// may still run: it goes back once its mark has passed
			// (_ShareRetireDone), not at once.
			if (share->pending >= 0) {
				share->images[share->pending].retiring = true;
				fShareRetiring = true;
			}
			share->pending = index;
		}
		damage = share->shape;
	}
	Damage(damage);
	if (fShareRetiring)
		release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
	return B_OK;
}


/*!	M11 fourth review: the window's ServerWindow is going, and its app may
	go before ~Window ends the shares (RemoveWindow). Until then
	ClippingChanged would look the views up in the app's token space. The
	shares end here; RemoveWindow finds none left.
*/
void
Compositor::ShareWindowGone(Window* window)
{
	bool ended = false;
	{
		BAutolock shareLock(fShareLock);
		for (int32 i = 0; i < fShares.CountItems(); i++) {
			Share* share = fShares.ItemAt(i);
			if (share->window != window)
				continue;
			if (!share->ended)
				share->giveBack = true;
			share->ended = true;
			share->window = NULL;
			ended = true;
		}
	}
	if (ended)
		release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
}


/*!	M11 fourth review: a view of this window was hidden, shown, moved,
	resized, scrolled or removed. That changes no window's clipping, so
	ClippingChanged does not run; the shared views of the window get their
	place again here, or the last image stays where the view was. From the
	window's ServerWindow thread, with the window read-locked.
*/
void
Compositor::ShareViewsChanged(Window* window)
{
	BRegion damage;
	{
		BAutolock shareLock(fShareLock);
		for (int32 i = 0; i < fShares.CountItems(); i++) {
			Share* share = fShares.ItemAt(i);
			if (share->window != window || share->ended)
				continue;
			BRegion before = share->shape;
			int32 left = share->left;
			int32 top = share->top;
			_ShareShape(share);
			if (before == share->shape && left == share->left
				&& top == share->top)
				continue;
			damage.Include(&before);
			damage.Include(&share->shape);
		}
	}
	if (damage.CountRects() > 0)
		Damage(damage);
}


/*!	The program's swapchain is gone. From the reply on, no semaphore of it
	is released; the imports are freed at the start of the next frame.
*/
void
Compositor::ShareEnd(team_id team, int32 viewToken, int32 shareId)
{
	BRegion damage;
	{
		BAutolock _(fShareLock);
		for (int32 i = 0; i < fShares.CountItems(); i++) {
			Share* share = fShares.ItemAt(i);
			if (share->id == shareId && share->team == team
				&& share->viewToken == viewToken) {
				share->ended = true;
				// its semaphores may be gone: release none of them
				share->giveBack = false;
				damage = share->shape;
			}
		}
	}
	Damage(damage);
	// M11 review: a hidden view has no damage; the frame frees the imports
	release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
}

