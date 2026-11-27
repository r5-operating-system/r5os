/*
 * Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include <OS.h>

#include <stdlib.h>
#include <stdio.h>

#include <libroot_private.h>
#include <pthread_private.h>
#include <runtime_loader.h>
#include <thread_defs.h>
#include <tls.h>
#include <syscalls.h>


#undef thread_entry
	// thread_entry is still defined in OS.h for compatibility reasons


typedef struct callback_node {
	struct callback_node *next;
	void (*function)(void *);
	void *argument;
} callback_node;


void _thread_do_exit_work(void);
void _thread_do_exit_notification(void);

// SPDX-SnippetBegin
// SPDX-License-Identifier: MIT
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors

/*	thread_local destructors (GitHub #3012, Haiku #18182).

	The compiler registers the destructor of a thread_local object through
	__cxa_thread_atexit(). libstdc++ only keeps its own list and does not keep
	the library loaded: when the library was unloaded with dlclose() before the
	thread ended, the thread called into unmapped code at its end. libroot now
	keeps the list. Each entry holds a reference to the image that registered
	it (the runtime loader's pin_image_at_address()), so dlclose() leaves that
	image mapped until the destructors of every thread that used it ran. The
	runtime loader binds __cxa_thread_atexit to libroot for every image.
*/

typedef struct thread_dtor_node {
	struct thread_dtor_node* next;
	void (*function)(void*);
	void* object;
	void* dso;
	void* image;
		// the pinned image this entry gives back, or NULL
} thread_dtor_node;


void _thread_call_tls_destructors(void);
int __cxa_thread_atexit_impl(void (*function)(void*), void* object,
	void* dsoHandle);
int __cxa_thread_atexit(void (*function)(void*), void* object,
	void* dsoHandle);


int
__cxa_thread_atexit_impl(void (*function)(void*), void* object,
	void* dsoHandle)
{
	thread_dtor_node** head
		= (thread_dtor_node**)tls_address(TLS_THREAD_DTORS_SLOT);
	thread_dtor_node* node;
	thread_dtor_node* other;
	void* key = dsoHandle != NULL ? dsoHandle : (void*)function;
	int pinned = 0;

	if (function == NULL)
		return -1;

	node = (thread_dtor_node*)malloc(sizeof(thread_dtor_node));
	if (node == NULL)
		return -1;

	node->function = function;
	node->object = object;
	node->dso = key;
	node->image = NULL;

	// One reference per image and thread is enough: all destructors run
	// before any reference is given back.
	for (other = *head; other != NULL; other = other->next) {
		if (other->dso == key && other->image != NULL) {
			pinned = 1;
			break;
		}
	}
	if (!pinned && __gRuntimeLoader->pin_image_at_address != NULL)
		node->image = __gRuntimeLoader->pin_image_at_address(key);

	node->next = *head;
	*head = node;
	return 0;
}


int
__cxa_thread_atexit(void (*function)(void*), void* object, void* dsoHandle)
{
	return __cxa_thread_atexit_impl(function, object, dsoHandle);
}


/*!	Runs the thread_local destructors of the current thread, newest first,
	including any a destructor registers while they run. Only then the image
	references are given back, which may unload a library that was closed
	while this thread still used it. Called at thread exit before the pthread
	keys and the TLS blocks go away, and by exit() before the static
	destructors.
*/
void
_thread_call_tls_destructors(void)
{
	thread_dtor_node** head
		= (thread_dtor_node**)tls_address(TLS_THREAD_DTORS_SLOT);
	thread_dtor_node* pins = NULL;
	thread_dtor_node* node;

	while ((node = *head) != NULL) {
		*head = node->next;
		node->function(node->object);

		if (node->image != NULL) {
			node->next = pins;
			pins = node;
		} else
			free(node);
	}

	while ((node = pins) != NULL) {
		pins = node->next;
		__gRuntimeLoader->unload_library(node->image);
		free(node);
	}
}
// SPDX-SnippetEnd


static status_t
thread_entry(void* _entry, void* _thread)
{
	thread_func entry = (thread_func)_entry;
	pthread_thread* thread = (pthread_thread*)_thread;
	status_t returnCode;

	__heap_thread_init();

	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	if (__gRuntimeLoader->init_initial_exec_tls != NULL)
		__gRuntimeLoader->init_initial_exec_tls();
	// SPDX-SnippetEnd

	returnCode = entry(thread->entry_argument);

	_thread_do_exit_work();
	__heap_thread_exit();

	return returnCode;
}


void
_thread_do_exit_notification(void)
{
	// empty stub for R5 compatibility
}


void
_thread_do_exit_work(void)
{
	callback_node *node;
	callback_node *next;

	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	_thread_call_tls_destructors();
	// SPDX-SnippetEnd

	node = tls_get(TLS_ON_EXIT_THREAD_SLOT);

	while (node != NULL) {
		next = node->next;

		node->function(node->argument);
		free(node);

		node = next;
	}

	tls_set(TLS_ON_EXIT_THREAD_SLOT, NULL);

	__pthread_destroy_thread();

	__gRuntimeLoader->destroy_thread_tls();
}


void
__set_stack_protection(void)
{
	if (__gABIVersion < B_HAIKU_ABI_GCC_2_HAIKU) {
		area_info info;
		ssize_t cookie = 0;

		while (get_next_area_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
			if ((info.protection & B_STACK_AREA) != 0) {
				_kern_set_area_protection(info.area,
					B_READ_AREA | B_WRITE_AREA | B_EXECUTE_AREA | B_STACK_AREA);
			}
		}
	}
}


// #pragma mark -


thread_id
spawn_thread(thread_func entry, const char *name, int32 priority, void *data)
{
	struct thread_creation_attributes attributes;
	pthread_thread* thread;
	thread_id id;

	thread = __allocate_pthread(NULL, data);
	if (thread == NULL)
		return B_NO_MEMORY;

	_single_threaded = false;
		// used for I/O locking - BeOS compatibility issue

	__pthread_init_creation_attributes(NULL, thread, &thread_entry, entry,
		thread, name, &attributes);
	thread->flags |= THREAD_DETACHED;

	attributes.priority = priority;

	id = _kern_spawn_thread(&attributes);
	if (id < 0)
		free(thread);
	else {
		thread->id = id;
		__set_stack_protection();
	}

	return id;
}


status_t
kill_thread(thread_id thread)
{
	return _kern_kill_thread(thread);
}


status_t
resume_thread(thread_id thread)
{
	return _kern_resume_thread(thread);
}


status_t
suspend_thread(thread_id thread)
{
	return _kern_suspend_thread(thread);
}


status_t
rename_thread(thread_id thread, const char *name)
{
	return _kern_rename_thread(thread, name);
}


status_t
set_thread_priority(thread_id thread, int32 priority)
{
	return _kern_set_thread_priority(thread, priority);
}


void
exit_thread(status_t status)
{
	_thread_do_exit_work();
	__heap_thread_exit();
	_kern_exit_thread(status);
}


status_t
wait_for_thread(thread_id thread, status_t *_returnCode)
{
	return _kern_wait_for_thread_etc(thread, 0, 0, _returnCode);
}


status_t
wait_for_thread_etc(thread_id thread, uint32 flags, bigtime_t timeout, status_t *_returnCode)
{
	return _kern_wait_for_thread_etc(thread, flags, timeout, _returnCode);
}


status_t
on_exit_thread(void (*callback)(void *), void *data)
{
	callback_node **head = (callback_node **)tls_address(TLS_ON_EXIT_THREAD_SLOT);

	callback_node *node = malloc(sizeof(callback_node));
	if (node == NULL)
		return B_NO_MEMORY;

	node->function = callback;
	node->argument = data;

	// add this node to the list
	node->next = *head;
	*head = node;

	return B_OK;
}


status_t
_get_thread_info(thread_id thread, thread_info *info, size_t size)
{
	if (info == NULL || size != sizeof(thread_info))
		return B_BAD_VALUE;

	return _kern_get_thread_info(thread, info);
}


status_t
_get_next_thread_info(team_id team, int32 *cookie, thread_info *info, size_t size)
{
	if (info == NULL || size != sizeof(thread_info))
		return B_BAD_VALUE;

	return _kern_get_next_thread_info(team, cookie, info);
}


status_t
send_data(thread_id thread, int32 code, const void *buffer, size_t bufferSize)
{
	return _kern_send_data(thread, code, buffer, bufferSize);
}


int32
receive_data(thread_id *_sender, void *buffer, size_t bufferSize)
{
	return _kern_receive_data(_sender, buffer, bufferSize);
}


bool
has_data(thread_id thread)
{
	return _kern_has_data(thread);
}


status_t
snooze_etc(bigtime_t timeout, int timeBase, uint32 flags)
{
	return _kern_snooze_etc(timeout, timeBase, flags, NULL);
}


status_t
snooze(bigtime_t timeout)
{
	return _kern_snooze_etc(timeout, B_SYSTEM_TIMEBASE, B_RELATIVE_TIMEOUT,
		NULL);
}


status_t
snooze_until(bigtime_t timeout, int timeBase)
{
	return _kern_snooze_etc(timeout, timeBase, B_ABSOLUTE_TIMEOUT, NULL);
}
