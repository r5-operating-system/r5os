/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Compositor Vulkan setup, window/frame buffer allocation, and direct scanout
 * import.
 */


#include "CompositorPrivate.h"

#include "compositor_shaders.h"
#include "r5_span_avx.h"


int64 gCompositorTotalWindowBytes = 0;


status_t
Compositor::_InitVulkan(bool radv, const char** _reason)
{
	_ExitVulkan();

	CompositorVulkan* vk = new(std::nothrow) CompositorVulkan;
	if (vk == NULL) {
		*_reason = "no memory";
		return B_NO_MEMORY;
	}
	memset(vk, 0, sizeof(*vk));
	fVulkan = vk;
	// buffers of an earlier device are not freed through this one
	fGeneration++;

	// M13 step 2 (owner, 16 Nov 2026): Mesa's own shader cache for
	// app_server (uid 0) goes in the system cache directory, not in the
	// administrator's ~/config/cache. Set before the driver loads; a
	// value already in the environment (a developer's) stays.
	{
		char cache[B_PATH_NAME_LENGTH];
		if (find_directory(B_SYSTEM_CACHE_DIRECTORY, -1, true, cache,
				sizeof(cache)) == B_OK) {
			strlcat(cache, "/app_server", sizeof(cache));
			mkdir(cache, 0755);
			strlcat(cache, "/mesa", sizeof(cache));
			mkdir(cache, 0755);
			unsetenv("MESA_SHADER_CACHE_DISABLE");
			setenv("MESA_SHADER_CACHE_DIR", cache, 0);
		}
	}
	vk->library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
	if (vk->library == NULL)
		vk->library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
	if (vk->library == NULL) {
		*_reason = "no vulkan loader";
		return B_ERROR;
	}
	vk->vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(vk->library,
		"vkGetInstanceProcAddr");
	if (vk->vkGetInstanceProcAddr == NULL) {
		*_reason = "no vulkan loader";
		return B_ERROR;
	}
	vk->vkCreateInstance = (PFN_vkCreateInstance)vk->vkGetInstanceProcAddr(
		NULL, "vkCreateInstance");

	VkApplicationInfo app = {};
	app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app.pApplicationName = "app_server";
	app.apiVersion = VK_API_VERSION_1_1;
	VkInstanceCreateInfo instanceInfo = {};
	instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.pApplicationInfo = &app;
	if (vk->vkCreateInstance == NULL
		|| vk->vkCreateInstance(&instanceInfo, NULL, &vk->instance)
			!= VK_SUCCESS) {
		*_reason = "no vulkan instance";
		return B_ERROR;
	}

#define VK_LOAD_INSTANCE(name) \
	vk->name = (PFN_##name)vk->vkGetInstanceProcAddr(vk->instance, #name); \
	if (vk->name == NULL) { *_reason = "vulkan " #name; return B_ERROR; }
	VK_INSTANCE_FUNCTIONS(VK_LOAD_INSTANCE)

	// RADV is any device that is not a CPU; Lavapipe is the CPU device.
	uint32 count = 8;
	VkPhysicalDevice devices[8];
	if (vk->vkEnumeratePhysicalDevices(vk->instance, &count, devices)
			< VK_SUCCESS || count == 0) {
		*_reason = "no vulkan device";
		return B_ERROR;
	}
	vk->physical = VK_NULL_HANDLE;
	for (uint32 i = 0; i < count; i++) {
		VkPhysicalDeviceProperties properties;
		vk->vkGetPhysicalDeviceProperties(devices[i], &properties);
		bool isCpu = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
		if (isCpu != radv) {
			vk->physical = devices[i];
			vk->timestampPeriod = properties.limits.timestampPeriod;
			strlcpy(vk->deviceName, properties.deviceName,
				sizeof(vk->deviceName));
			break;
		}
	}
	if (vk->physical == VK_NULL_HANDLE) {
		*_reason = radv ? "radv not found" : "lavapipe not found";
		return B_ERROR;
	}

	uint32 extensionCount = 0;
	vk->vkEnumerateDeviceExtensionProperties(vk->physical, NULL,
		&extensionCount, NULL);
	VkExtensionProperties* extensions
		= new(std::nothrow) VkExtensionProperties[extensionCount + 1];
	bool pushDescriptor = false;
	bool memoryFd = false;
	bool dmaBuf = false;
	if (extensions != NULL) {
		vk->vkEnumerateDeviceExtensionProperties(vk->physical, NULL,
			&extensionCount, extensions);
		for (uint32 i = 0; i < extensionCount; i++) {
			if (strcmp(extensions[i].extensionName,
					VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) == 0)
				pushDescriptor = true;
			if (strcmp(extensions[i].extensionName,
					VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME) == 0)
				memoryFd = true;
			if (strcmp(extensions[i].extensionName,
					VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME) == 0)
				dmaBuf = true;
		}
		delete[] extensions;
	}
	// M11 step 3b: shared views are imported on RADV only.
	vk->canImport = radv && memoryFd && dmaBuf;
	if (!pushDescriptor) {
		*_reason = "no push descriptor";
		return B_ERROR;
	}

	uint32 familyCount = 8;
	VkQueueFamilyProperties families[8];
	vk->vkGetPhysicalDeviceQueueFamilyProperties(vk->physical, &familyCount,
		families);
	// The queue stays a graphics queue (decision 16): the R5 DRM library
	// accepts only GFX submissions, so a compute-only family would make
	// every submit fail. The compose is a compute dispatch on that queue.
	uint32 family = UINT32_MAX;
	for (uint32 i = 0; i < familyCount; i++) {
		log_line("vulkan queue family %" B_PRIu32 " flags %#" B_PRIx32, i,
			(uint32)families[i].queueFlags);
		const VkQueueFlags wanted = VK_QUEUE_GRAPHICS_BIT
			| VK_QUEUE_COMPUTE_BIT;
		if (family == UINT32_MAX
			&& (families[i].queueFlags & wanted) == wanted)
			family = i;
	}
	if (family == UINT32_MAX) {
		*_reason = "no graphics queue";
		return B_ERROR;
	}

	// Host-visible and coherent memory for the window buffers and the
	// frame: Painter writes the window buffers, the compose reads them.
	// Host-cached first. DEVICE_LOCAL is not excluded: RADV may mark GTT
	// that way on an APU (decision 5).
	VkPhysicalDeviceMemoryProperties memory;
	vk->vkGetPhysicalDeviceMemoryProperties(vk->physical, &memory);
	vk->memoryType = UINT32_MAX;
	const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
		| VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	for (int pass = 0; pass < 2 && vk->memoryType == UINT32_MAX; pass++) {
		for (uint32 i = 0; i < memory.memoryTypeCount; i++) {
			VkMemoryPropertyFlags flags = memory.memoryTypes[i].propertyFlags;
			if ((flags & wanted) != wanted)
				continue;
			// first pass: also cached, because Painter reads what it
			// blends
			if (pass == 0 && (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) == 0)
				continue;
			vk->memoryType = i;
			break;
		}
	}
	if (vk->memoryType == UINT32_MAX) {
		*_reason = "no host memory";
		return B_ERROR;
	}

	float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = {};
	queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueInfo.queueFamilyIndex = family;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;
	const char* deviceExtensions[] = { VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
		VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
		VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME };
	VkDeviceCreateInfo deviceInfo = {};
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	deviceInfo.enabledExtensionCount = vk->canImport ? 3 : 1;
	deviceInfo.ppEnabledExtensionNames = deviceExtensions;
	vk->family = family;
	vk->extensionCount = deviceInfo.enabledExtensionCount;
	for (uint32 i = 0; i < vk->extensionCount; i++)
		vk->extensions[i] = deviceExtensions[i];
	if (vk->vkCreateDevice(vk->physical, &deviceInfo, NULL, &vk->device)
			!= VK_SUCCESS) {
		*_reason = "no vulkan device";
		return B_ERROR;
	}

#define VK_LOAD_DEVICE(name) \
	vk->name = (PFN_##name)vk->vkGetDeviceProcAddr(vk->device, #name); \
	if (vk->name == NULL) { *_reason = "vulkan " #name; return B_ERROR; }
	VK_DEVICE_FUNCTIONS(VK_LOAD_DEVICE)

	vk->vkGetDeviceQueue(vk->device, family, 0, &vk->queue);

	// The R5 DRM library RADV loaded: dlopen of a loaded library returns
	// that image, so these calls see RADV's import table.
	if (vk->canImport) {
		vk->drm = dlopen("libdrm.so.2", RTLD_NOW | RTLD_LOCAL);
		if (vk->drm != NULL) {
			vk->tokenToFd = (r5_token_to_fd_hook)dlsym(vk->drm,
				"r5_drm_token_to_fd");
			vk->markGet = (r5_mark_get_hook)dlsym(vk->drm, "r5_drm_mark_get");
			vk->importSize = (r5_import_size_hook)dlsym(vk->drm,
				"r5_drm_import_size");
		}
		if (vk->tokenToFd == NULL || vk->markGet == NULL
			|| vk->importSize == NULL)
			vk->canImport = false;
	}

	VkShaderModuleCreateInfo shaderInfo = {};
	shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shaderInfo.codeSize = sizeof(kComposeSpirv);
	shaderInfo.pCode = kComposeSpirv;
	VkShaderModule shader;
	if (vk->vkCreateShaderModule(vk->device, &shaderInfo, NULL, &shader)
			!= VK_SUCCESS) {
		*_reason = "shader";
		return B_ERROR;
	}

	// M13 step 2: two storage buffers and the window's image
	VkDescriptorSetLayoutBinding bindings[3] = {};
	for (int i = 0; i < 3; i++) {
		bindings[i].binding = i;
		bindings[i].descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
			: VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}
	VkDescriptorSetLayoutCreateInfo setInfo = {};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
	setInfo.bindingCount = 3;
	setInfo.pBindings = bindings;
	VkPushConstantRange range = {};
	range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	range.size = sizeof(compose_rect);
	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &vk->setLayout;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &range;
	VkComputePipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.stage.sType
		= VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipelineInfo.stage.module = shader;
	pipelineInfo.stage.pName = "main";
	bool pipelineOk
		= vk->vkCreateDescriptorSetLayout(vk->device, &setInfo, NULL,
			&vk->setLayout) == VK_SUCCESS
		&& vk->vkCreatePipelineLayout(vk->device, &layoutInfo, NULL,
			&vk->pipelineLayout) == VK_SUCCESS;
	if (pipelineOk) {
		pipelineInfo.layout = vk->pipelineLayout;
		pipelineOk = vk->vkCreateComputePipelines(vk->device, VK_NULL_HANDLE,
			1, &pipelineInfo, NULL, &vk->pipeline) == VK_SUCCESS;
	}
	vk->vkDestroyShaderModule(vk->device, shader, NULL);
	if (!pipelineOk) {
		*_reason = "pipeline";
		return B_ERROR;
	}

	// M11 step 4 (decision 16): the second compute pipeline, for the
	// effects. Three storage buffers: a source, the target, and the kept
	// blur for the glass.
	shaderInfo.codeSize = sizeof(kEffectsSpirv);
	shaderInfo.pCode = kEffectsSpirv;
	if (vk->vkCreateShaderModule(vk->device, &shaderInfo, NULL, &shader)
			!= VK_SUCCESS) {
		*_reason = "effects shader";
		return B_ERROR;
	}
	// M13 step 2: and the window's image
	VkDescriptorSetLayoutBinding effectBindings[4] = {};
	for (int i = 0; i < 4; i++) {
		effectBindings[i].binding = i;
		effectBindings[i].descriptorType = i < 3
			? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
			: VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		effectBindings[i].descriptorCount = 1;
		effectBindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}
	setInfo.bindingCount = 4;
	setInfo.pBindings = effectBindings;
	range.size = sizeof(effect_rect);
	layoutInfo.pSetLayouts = &vk->effectsSetLayout;
	pipelineInfo.stage.module = shader;
	pipelineOk
		= vk->vkCreateDescriptorSetLayout(vk->device, &setInfo, NULL,
			&vk->effectsSetLayout) == VK_SUCCESS
		&& vk->vkCreatePipelineLayout(vk->device, &layoutInfo, NULL,
			&vk->effectsPipelineLayout) == VK_SUCCESS;
	if (pipelineOk) {
		pipelineInfo.layout = vk->effectsPipelineLayout;
		pipelineOk = vk->vkCreateComputePipelines(vk->device, VK_NULL_HANDLE,
			1, &pipelineInfo, NULL, &vk->effectsPipeline) == VK_SUCCESS;
	}
	vk->vkDestroyShaderModule(vk->device, shader, NULL);
	if (!pipelineOk) {
		*_reason = "effects pipeline";
		return B_ERROR;
	}

	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = family;
	VkCommandBufferAllocateInfo commandInfo = {};
	commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandInfo.commandBufferCount = 1;
	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	if (vk->vkCreateCommandPool(vk->device, &poolInfo, NULL, &vk->commandPool)
			!= VK_SUCCESS) {
		*_reason = "command pool";
		return B_ERROR;
	}
	commandInfo.commandPool = vk->commandPool;
	if (vk->vkAllocateCommandBuffers(vk->device, &commandInfo, &vk->commands)
			!= VK_SUCCESS
		|| vk->vkCreateFence(vk->device, &fenceInfo, NULL, &vk->fence)
			!= VK_SUCCESS) {
		*_reason = "command buffer";
		return B_ERROR;
	}
	if (!_InitImages(_reason))
		return B_ERROR;
	return B_OK;
}


status_t
Compositor::_InitSharedVulkan(CompositorVulkan* shared, const char** _reason)
{
	_ExitVulkan();

	CompositorVulkan* vk = new(std::nothrow) CompositorVulkan;
	if (vk == NULL) {
		*_reason = "no memory";
		return B_NO_MEMORY;
	}
	memcpy(vk, shared, sizeof(*vk));
	vk->shared = true;
	vk->commandPool = VK_NULL_HANDLE;
	vk->commands = VK_NULL_HANDLE;
	vk->fence = VK_NULL_HANDLE;
	vk->timestamps = VK_NULL_HANDLE;
	vk->frame = VK_NULL_HANDLE;
	vk->frameMemory = VK_NULL_HANDLE;
	vk->frameBits = NULL;
	vk->scratch = VK_NULL_HANDLE;
	vk->scratchMemory = VK_NULL_HANDLE;
	vk->scratchBytes = 0;
	vk->slideOld = VK_NULL_HANDLE;
	vk->slideOldMemory = VK_NULL_HANDLE;
	vk->slideNew = VK_NULL_HANDLE;
	vk->slideNewMemory = VK_NULL_HANDLE;
	vk->slideBytes = 0;
	vk->scanActive = false;
	vk->scanGeneration = 0;
	vk->scanFailedGeneration = 0;
	vk->back = VK_NULL_HANDLE;
	vk->backMemory = VK_NULL_HANDLE;
	vk->backPitch = 0;
	vk->scanout = VK_NULL_HANDLE;
	vk->scanoutMemory = VK_NULL_HANDLE;
	vk->scanPitch = 0;

	fVulkan = vk;
	fGeneration++;

	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = vk->family;
	if (vk->vkCreateCommandPool(vk->device, &poolInfo, NULL, &vk->commandPool)
			!= VK_SUCCESS) {
		*_reason = "command pool";
		return B_ERROR;
	}
	VkCommandBufferAllocateInfo commandInfo = {};
	commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandInfo.commandPool = vk->commandPool;
	commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandInfo.commandBufferCount = 1;
	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	if (vk->vkAllocateCommandBuffers(vk->device, &commandInfo, &vk->commands)
			!= VK_SUCCESS
		|| vk->vkCreateFence(vk->device, &fenceInfo, NULL, &vk->fence)
			!= VK_SUCCESS) {
		*_reason = "command buffer";
		return B_ERROR;
	}
	if (fDebug && vk->timestampPeriod > 0) {
		VkQueryPoolCreateInfo queryInfo = {};
		queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
		queryInfo.queryCount = kPassStamps;
		if (vk->vkCreateQueryPool(vk->device, &queryInfo, NULL,
				&vk->timestamps) != VK_SUCCESS)
			vk->timestamps = VK_NULL_HANDLE;
	}
	return B_OK;
}


/*!	M13 step 2: the sampler of the window images, and the 1x1 image bound
	when a pass reads a buffer, made ready for the sampler once.
*/
bool
Compositor::_InitImages(const char** _reason)
{
	CompositorVulkan* vk = fVulkan;
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	if (vk->vkCreateSampler(vk->device, &samplerInfo, NULL, &vk->sampler)
			!= VK_SUCCESS) {
		*_reason = "sampler";
		return false;
	}

	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
	imageInfo.extent.width = 1;
	imageInfo.extent.height = 1;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (vk->vkCreateImage(vk->device, &imageInfo, NULL, &vk->blankImage)
			!= VK_SUCCESS) {
		*_reason = "blank image";
		return false;
	}
	VkMemoryRequirements requirements;
	vk->vkGetImageMemoryRequirements(vk->device, vk->blankImage,
		&requirements);
	VkPhysicalDeviceMemoryProperties memory;
	vk->vkGetPhysicalDeviceMemoryProperties(vk->physical, &memory);
	bool bound = false;
	for (uint32 i = 0; i < memory.memoryTypeCount && !bound; i++) {
		if ((requirements.memoryTypeBits & (1u << i)) == 0)
			continue;
		VkMemoryAllocateInfo allocateInfo = {};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = requirements.size;
		allocateInfo.memoryTypeIndex = i;
		if (vk->vkAllocateMemory(vk->device, &allocateInfo, NULL,
				&vk->blankMemory) != VK_SUCCESS)
			continue;
		bound = vk->vkBindImageMemory(vk->device, vk->blankImage,
			vk->blankMemory, 0) == VK_SUCCESS;
		if (!bound) {
			vk->vkFreeMemory(vk->device, vk->blankMemory, NULL);
			vk->blankMemory = VK_NULL_HANDLE;
		}
	}
	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = vk->blankImage;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = imageInfo.format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 1;
	if (!bound || vk->vkCreateImageView(vk->device, &viewInfo, NULL,
			&vk->blankView) != VK_SUCCESS) {
		*_reason = "blank image";
		return false;
	}

	if (fDebug && vk->timestampPeriod > 0) {
		// M13 step 2: the compose time per pass (gpu_debug only); without
		// it the frame has no timestamps
		VkQueryPoolCreateInfo queryInfo = {};
		queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
		queryInfo.queryCount = kPassStamps;
		if (vk->vkCreateQueryPool(vk->device, &queryInfo, NULL,
				&vk->timestamps) != VK_SUCCESS)
			vk->timestamps = VK_NULL_HANDLE;
	}

	VkCommandBufferBeginInfo begin = {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vk->vkBeginCommandBuffer(vk->commands, &begin);
	VkImageMemoryBarrier toRead = {};
	toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	toRead.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toRead.image = vk->blankImage;
	toRead.subresourceRange = viewInfo.subresourceRange;
	vk->vkCmdPipelineBarrier(vk->commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
		&toRead);
	vk->vkEndCommandBuffer(vk->commands);
	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &vk->commands;
	vk->vkResetFences(vk->device, 1, &vk->fence);
	SkiaGpu::LockQueue();
	VkResult result = vk->vkQueueSubmit(vk->queue, 1, &submit, vk->fence);
	SkiaGpu::UnlockQueue();
	if (result != VK_SUCCESS
		|| vk->vkWaitForFences(vk->device, 1, &vk->fence, VK_TRUE,
			kFenceLimit * 1000) != VK_SUCCESS) {
		*_reason = "blank image";
		return false;
	}
	return true;
}


void
Compositor::_ExitVulkan()
{
	CompositorVulkan* vk = fVulkan;
	if (vk == NULL)
		return;
	fVulkan = NULL;
	// M11 review: a job that never ended; waiting for idle would block
	// app_server, freeing would hand the GPU's memory to someone else.
	if (atomic_get(&fGpuBusy) != 0) {
		_ReturnSharedImages();
		log_line("compositor device kept: the gpu is still busy");
		return;
	}

	// A lost device: WaitIdle does not return while the ring is stuck,
	// and vkDestroyDevice then runs while a window still has a recorder
	// (stick 61, VulkanResourceProvider). Leave the device in place.
	// The engines are already on the screen.
	if (fSkia != NULL && fSkia->Abandoned()) {
		if (!vk->shared) {
			_StopDrawThread();
			fSkia->DestroyContext();
			if (!fOnLavapipe)
				log_line("left radv");
		}
		delete vk;
		fFrameWidth = 0;
		fFrameHeight = 0;
		return;
	}

	// A device that failed half way has some functions not loaded; they
	// are loaded in this order, so the last one tells all were.
	if (vk->device != VK_NULL_HANDLE && vk->vkDeviceWaitIdle != NULL) {
		SkiaGpu::LockQueue();
		vk->vkDeviceWaitIdle(vk->device);
		SkiaGpu::UnlockQueue();
		// M12: Skia's context and every GPU image of it, before the device
		if (!vk->shared && fSkia != NULL) {
			_StopDrawThread();
			fSkia->DestroyContext();
		}
		_ScanoutFree(vk);
		// M11 step 3b: the imports of shared views go with this device;
		// the programs get a refusal at their next present. Step 5: the
		// images app_server holds go back first.
		_ReturnSharedImages();
		{
			BAutolock _(fShareLock);
			fVulkan = vk;
			for (int32 i = fShares.CountItems(); i-- > 0;) {
				Share* share = fShares.ItemAt(i);
				_ShareFree(share);
				fShares.RemoveItemAt(i);
				delete share;
			}
			fVulkan = NULL;
		}
		if (vk->frame != VK_NULL_HANDLE)
			vk->vkDestroyBuffer(vk->device, vk->frame, NULL);
		if (vk->frameMemory != VK_NULL_HANDLE)
			vk->vkFreeMemory(vk->device, vk->frameMemory, NULL);
		if (vk->scratch != VK_NULL_HANDLE)
			vk->vkDestroyBuffer(vk->device, vk->scratch, NULL);
		if (vk->scratchMemory != VK_NULL_HANDLE)
			vk->vkFreeMemory(vk->device, vk->scratchMemory, NULL);
		if (vk->slideOld != VK_NULL_HANDLE)
			vk->vkDestroyBuffer(vk->device, vk->slideOld, NULL);
		if (vk->slideOldMemory != VK_NULL_HANDLE)
			vk->vkFreeMemory(vk->device, vk->slideOldMemory, NULL);
		if (vk->slideNew != VK_NULL_HANDLE)
			vk->vkDestroyBuffer(vk->device, vk->slideNew, NULL);
		if (vk->slideNewMemory != VK_NULL_HANDLE)
			vk->vkFreeMemory(vk->device, vk->slideNewMemory, NULL);
		if (vk->timestamps != VK_NULL_HANDLE)
			vk->vkDestroyQueryPool(vk->device, vk->timestamps, NULL);
		if (vk->fence != VK_NULL_HANDLE)
			vk->vkDestroyFence(vk->device, vk->fence, NULL);
		if (vk->commandPool != VK_NULL_HANDLE)
			vk->vkDestroyCommandPool(vk->device, vk->commandPool, NULL);
		if (vk->shared) {
			delete vk;
			fFrameWidth = 0;
			fFrameHeight = 0;
			return;
		}
		if (vk->effectsPipeline != VK_NULL_HANDLE)
			vk->vkDestroyPipeline(vk->device, vk->effectsPipeline, NULL);
		if (vk->effectsPipelineLayout != VK_NULL_HANDLE) {
			vk->vkDestroyPipelineLayout(vk->device, vk->effectsPipelineLayout,
				NULL);
		}
		if (vk->effectsSetLayout != VK_NULL_HANDLE) {
			vk->vkDestroyDescriptorSetLayout(vk->device, vk->effectsSetLayout,
				NULL);
		}
		if (vk->blankView != VK_NULL_HANDLE)
			vk->vkDestroyImageView(vk->device, vk->blankView, NULL);
		if (vk->blankImage != VK_NULL_HANDLE)
			vk->vkDestroyImage(vk->device, vk->blankImage, NULL);
		if (vk->blankMemory != VK_NULL_HANDLE)
			vk->vkFreeMemory(vk->device, vk->blankMemory, NULL);
		if (vk->sampler != VK_NULL_HANDLE)
			vk->vkDestroySampler(vk->device, vk->sampler, NULL);
		if (vk->pipeline != VK_NULL_HANDLE)
			vk->vkDestroyPipeline(vk->device, vk->pipeline, NULL);
		if (vk->pipelineLayout != VK_NULL_HANDLE)
			vk->vkDestroyPipelineLayout(vk->device, vk->pipelineLayout, NULL);
		if (vk->setLayout != VK_NULL_HANDLE)
			vk->vkDestroyDescriptorSetLayout(vk->device, vk->setLayout, NULL);
	}
	// M13 step 2: a window image still held (a closing window's fade)
	// must not reach a destroyed device
	SkiaGpu::KeepImageMemory();
	if (vk->device != VK_NULL_HANDLE && vk->vkDestroyDevice != NULL)
		vk->vkDestroyDevice(vk->device, NULL);
	if (vk->instance != VK_NULL_HANDLE && vk->vkDestroyInstance != NULL)
		vk->vkDestroyInstance(vk->instance, NULL);
	if (vk->drm != NULL)
		dlclose(vk->drm);
	if (vk->library != NULL)
		dlclose(vk->library);
	delete vk;
	fFrameWidth = 0;
	fFrameHeight = 0;
}


bool
compositor_allocate_host_buffer(CompositorVulkan* vk, uint64 bytes,
	VkBuffer* _buffer, VkDeviceMemory* _memory, void** _bits)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = bytes;
	// M13: the frame is also a transfer target (the image copy), and the
	// compare's copies are written by vkCmdCopyImageToBuffer
	bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
		| VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vk->vkCreateBuffer(vk->device, &bufferInfo, NULL, _buffer)
			!= VK_SUCCESS)
		return false;

	VkMemoryRequirements requirements;
	vk->vkGetBufferMemoryRequirements(vk->device, *_buffer, &requirements);
	VkMemoryAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = requirements.size;
	allocateInfo.memoryTypeIndex = vk->memoryType;
	if ((requirements.memoryTypeBits & (1u << vk->memoryType)) == 0
		|| vk->vkAllocateMemory(vk->device, &allocateInfo, NULL, _memory)
			!= VK_SUCCESS) {
		vk->vkDestroyBuffer(vk->device, *_buffer, NULL);
		return false;
	}
	if (vk->vkBindBufferMemory(vk->device, *_buffer, *_memory, 0)
			!= VK_SUCCESS
		|| vk->vkMapMemory(vk->device, *_memory, 0, VK_WHOLE_SIZE, 0, _bits)
			!= VK_SUCCESS) {
		vk->vkDestroyBuffer(vk->device, *_buffer, NULL);
		vk->vkFreeMemory(vk->device, *_memory, NULL);
		return false;
	}
	return true;
}


/*!	One pass the way Painter writes a window: a fill (stores only) or a
	blend (each pixel read, mixed and written back). Returns the time of
	the faster of three runs. The sum keeps the stores from being dropped.
*/
static bigtime_t
time_write(uint32* bits, uint64 pixels, bool blend, volatile uint32* sink)
{
	bigtime_t best = B_INFINITE_TIMEOUT;
	for (int run = 0; run < 3; run++) {
		bigtime_t start = system_time();
		if (blend)
			r5_span_blend(bits, pixels);
		else {
			uint32 color = 0xff336699 + run;
			for (uint64 i = 0; i < pixels; i++)
				bits[i] = color;
		}
		bigtime_t took = system_time() - start;
		if (took < best)
			best = took;
		*sink += bits[pixels / 2];
	}
	return best;
}


/*!	M11 stick, gpu_debug: the window buffer memory against malloc, once,
	with the size of the first window buffer. A separate buffer, so no
	window shows the test pattern. Decision 5 wants the window buffer
	cached; an uncached one is many times slower here.
*/
static void
log_write_test(CompositorVulkan* vk, uint64 bytes)
{
	VkBuffer buffer;
	VkDeviceMemory memory;
	void* bits;
	if (!compositor_allocate_host_buffer(vk, bytes, &buffer, &memory, &bits)) {
		log_line("window buffer write test skipped: no memory");
		return;
	}
	uint32* heap = (uint32*)malloc(bytes);
	if (heap == NULL) {
		vk->vkDestroyBuffer(vk->device, buffer, NULL);
		vk->vkFreeMemory(vk->device, memory, NULL);
		log_line("window buffer write test skipped: no memory");
		return;
	}
	uint64 pixels = bytes / 4;
	volatile uint32 sink = 0;
	bigtime_t bufferFill = time_write((uint32*)bits, pixels, false, &sink);
	bigtime_t bufferBlend = time_write((uint32*)bits, pixels, true, &sink);
	bigtime_t heapFill = time_write(heap, pixels, false, &sink);
	bigtime_t heapBlend = time_write(heap, pixels, true, &sink);
	free(heap);
	vk->vkDestroyBuffer(vk->device, buffer, NULL);
	vk->vkFreeMemory(vk->device, memory, NULL);
	log_line("window buffer write fill %" B_PRId64 " us blend %" B_PRId64
		" us, malloc fill %" B_PRId64 " us blend %" B_PRId64 " us, %"
		B_PRIu64 " KB", bufferFill, bufferBlend, heapFill, heapBlend,
		bytes / 1024);
}


WindowBuffer*
Compositor::_AllocateBuffer(uint32 width, uint32 height, bool withMemory)
{
	if (fVulkan == NULL || width == 0 || height == 0)
		return NULL;
	uint64 bytes = (uint64)width * height * 4;
	if ((uint64)atomic_get64(&gCompositorTotalWindowBytes) + bytes > fBudget)
		return NULL;
	if (!withMemory) {
		// M13 step 2: a window that Skia draws; its image (made by the
		// engine, SkiaTarget::SetSurface()) is what counts in the window
		// memory, with the same size
		WindowBuffer* windowBuffer = new(std::nothrow) WindowBuffer(this,
			NULL, width, height, 0, 0, fGeneration);
		if (windowBuffer != NULL) {
			atomic_add64((int64*)&fWindowBytes, bytes);
			atomic_add64(&gCompositorTotalWindowBytes, bytes);
		}
		return windowBuffer;
	}

	VkBuffer buffer;
	VkDeviceMemory memory;
	void* bits;
	if (!compositor_allocate_host_buffer(fVulkan, bytes, &buffer, &memory, &bits))
		return NULL;
	// Vulkan does not promise cleared memory: Lavapipe hands out reused
	// heap memory (RADV's GTT comes cleared from the kernel). Skia takes a
	// new buffer's pixels as the window's first image (SkiaTarget::
	// SetSurface()), so the old pixels showed until the window drew
	// (QEMU and the 5625U's boot 3, 16 Nov 2026). Transparent instead.
	memset(bits, 0, (size_t)bytes);

	WindowBuffer* windowBuffer = new(std::nothrow) WindowBuffer(this, bits,
		width, height, (uint64)buffer, (uint64)memory, fGeneration);
	if (windowBuffer == NULL) {
		fVulkan->vkDestroyBuffer(fVulkan->device, buffer, NULL);
		fVulkan->vkFreeMemory(fVulkan->device, memory, NULL);
		return NULL;
	}
	atomic_add64((int64*)&fWindowBytes, bytes);
	atomic_add64(&gCompositorTotalWindowBytes, bytes);

	static bool sLogged = false;
	if (!sLogged) {
		sLogged = true;
		VkPhysicalDeviceMemoryProperties memory;
		fVulkan->vkGetPhysicalDeviceMemoryProperties(fVulkan->physical,
			&memory);
		log_line("window memory type %" B_PRIu32 " flags %#" B_PRIx32,
			fVulkan->memoryType,
			(uint32)memory.memoryTypes[fVulkan->memoryType].propertyFlags);
		if (fDebug)
			log_write_test(fVulkan, bytes);
	}
	return windowBuffer;
}


void
Compositor::_FreeBuffer(WindowBuffer* buffer, uint64 vulkanBuffer,
	uint64 vulkanMemory)
{
	// Review after M11: a buffer kept because the GPU may still use it
	// (fGpuBusy) still counts in the window memory.
	if (atomic_get(&fGpuBusy) != 0)
		return;
	atomic_add64((int64*)&fWindowBytes, -(int64)buffer->Bytes());
	atomic_add64(&gCompositorTotalWindowBytes, -(int64)buffer->Bytes());
	if (fVulkan == NULL || buffer->Generation() != fGeneration
		|| vulkanBuffer == 0)
		return;
	fVulkan->vkDestroyBuffer(fVulkan->device, (VkBuffer)vulkanBuffer, NULL);
	fVulkan->vkFreeMemory(fVulkan->device, (VkDeviceMemory)vulkanMemory,
		NULL);
}


/*!	Allocates a window buffer. Over the budget, the buffers of windows that
	are not on screen now (hidden, minimised, on another workspace) go
	first, the one shown longest ago first; such a window draws all of
	itself again when it shows. Called with all windows and the entry list
	locked. A window without a buffer draws nothing: its drawable regions
	are empty (Window::_ClipToBuffer).
*/
WindowBuffer*
Compositor::_AllocateOrMakeRoom(uint32 width, uint32 height,
	const Entry* keep, bool memory)
{
	WindowBuffer* buffer = _AllocateBuffer(width, height, memory);
	int32 workspace = fDesktop->CurrentWorkspace();
	while (buffer == NULL) {
		Entry* oldest = NULL;
		for (int32 i = 0; i < fEntries.CountItems(); i++) {
			Entry* entry = fEntries.ItemAt(i);
			if (entry == keep || entry->iface->Buffer() == NULL
				|| entry->window == NULL)
				continue;
			// only windows that are not on screen now
			if (!entry->window->IsHidden()
				&& entry->window->InWorkspace(workspace))
				continue;
			if (oldest == NULL || entry->lastShown < oldest->lastShown)
				oldest = entry;
		}
		if (oldest == NULL)
			return NULL;
		oldest->iface->SetBuffer(NULL, 0, 0);
		if (SkiaDrawingEngine* skia = dynamic_cast<SkiaDrawingEngine*>(
				oldest->window->GetDrawingEngine()))
			skia->ReleaseBuffer();
		// M11 review: the last layer list still holds it, or nothing is
		// freed and every off-screen window loses its buffer for nothing.
		// The compose skips a layer without a buffer; a frame in flight
		// keeps its own reference until its fence.
		{
			BAutolock layerLock(fLayerLock);
			for (int32 i = 0; i < fLayers->CountItems(); i++) {
				Layer* layer = fLayers->ItemAt(i);
				if (layer->entry.Get() == oldest)
					layer->buffer.Unset();
			}
		}
		// M11 review: a closing window's fade holds it too; it ends early.
		// (A frame in flight still holds it until its fence: near the
		// budget that can still end the GPU mode.)
		BRegion faded;
		{
			BAutolock animLock(fAnimLock);
			for (int32 i = fGhosts.CountItems(); i-- > 0;) {
				Ghost* ghost = fGhosts.ItemAt(i);
				if (ghost->entry.Get() != oldest)
					continue;
				faded.Include(&ghost->area);
				delete fGhosts.RemoveItemAt(i);
			}
		}
		Damage(faded);
		buffer = _AllocateBuffer(width, height, memory);
	}
	return buffer;
}


bool
Compositor::_EnsureFrame(uint32 width, uint32 height)
{
	CompositorVulkan* vk = fVulkan;
	if (width == fFrameWidth && height == fFrameHeight && vk->frame != 0)
		return true;

	if (vk->frame != VK_NULL_HANDLE) {
		vk->vkDestroyBuffer(vk->device, vk->frame, NULL);
		vk->vkFreeMemory(vk->device, vk->frameMemory, NULL);
		vk->frame = VK_NULL_HANDLE;
		vk->frameMemory = VK_NULL_HANDLE;
	}
	free(fScratch);
	fScratch = NULL;
	fFrameWidth = 0;
	fFrameHeight = 0;

	void* bits;
	if (!compositor_allocate_host_buffer(vk, (uint64)width * height * 4, &vk->frame,
			&vk->frameMemory, &bits))
		return false;
	vk->frameBits = (uint32*)bits;
	fFrameWidth = width;
	fFrameHeight = height;
	if (fDebug)
		fScratch = (uint32*)malloc((size_t)width * height * 4);
	_UpdatePeriod();
	return true;
}




//	#pragma mark - scanout (M11 step 3b, decision 4)


//!	Frees the imports of the back buffer and the scanout. GPU idle.
void
Compositor::_ScanoutFree(CompositorVulkan* vk)
{
	if (vk->back != VK_NULL_HANDLE)
		vk->vkDestroyBuffer(vk->device, vk->back, NULL);
	if (vk->backMemory != VK_NULL_HANDLE)
		vk->vkFreeMemory(vk->device, vk->backMemory, NULL);
	if (vk->scanout != VK_NULL_HANDLE)
		vk->vkDestroyBuffer(vk->device, vk->scanout, NULL);
	if (vk->scanoutMemory != VK_NULL_HANDLE)
		vk->vkFreeMemory(vk->device, vk->scanoutMemory, NULL);
	vk->back = VK_NULL_HANDLE;
	vk->backMemory = VK_NULL_HANDLE;
	vk->scanout = VK_NULL_HANDLE;
	vk->scanoutMemory = VK_NULL_HANDLE;
	vk->scanActive = false;
}


//!	One import of app_server's own export: a VkBuffer over it.
static bool
import_own(CompositorVulkan* vk, const uint8 token[16], uint64 size,
	VkBufferUsageFlags usage, bool deviceLocal, VkBuffer* _buffer,
	VkDeviceMemory* _memory)
{
	VkExternalMemoryBufferCreateInfo external = {};
	external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
	external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = &external;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vk->vkCreateBuffer(vk->device, &bufferInfo, NULL, _buffer)
			!= VK_SUCCESS) {
		*_buffer = VK_NULL_HANDLE;
		return false;
	}
	VkMemoryRequirements requirements;
	vk->vkGetBufferMemoryRequirements(vk->device, *_buffer, &requirements);
	VkPhysicalDeviceMemoryProperties memory;
	vk->vkGetPhysicalDeviceMemoryProperties(vk->physical, &memory);
	uint32 type = UINT32_MAX;
	for (uint32 i = 0; i < memory.memoryTypeCount; i++) {
		if ((requirements.memoryTypeBits & (1u << i)) == 0)
			continue;
		bool local = (memory.memoryTypes[i].propertyFlags
			& VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
		if (type == UINT32_MAX || (deviceLocal && local)
			|| (!deviceLocal && i == vk->memoryType)) {
			type = i;
			if ((deviceLocal && local) || (!deviceLocal && i == vk->memoryType))
				break;
		}
	}
	int fd = -1;
	uint32 id = 0;
	team_id self = getpid();
	if (type == UINT32_MAX
		|| vk->tokenToFd(-1, token, self, &fd, &id) != 0)
		return false;
	VkImportMemoryFdInfoKHR import = {};
	import.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
	import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
	import.fd = fd;
	VkMemoryAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.pNext = &import;
	allocateInfo.allocationSize = requirements.size;
	allocateInfo.memoryTypeIndex = type;
	if (vk->vkAllocateMemory(vk->device, &allocateInfo, NULL, _memory)
			!= VK_SUCCESS) {
		*_memory = VK_NULL_HANDLE;
		close(fd);
		return false;
	}
	uint64 bytes = 0;
	if (vk->importSize(id, &bytes) != 0 || requirements.size > bytes
		|| size > bytes)
		return false;
	return vk->vkBindBufferMemory(vk->device, *_buffer, *_memory, 0)
		== VK_SUCCESS;
}


/*!	Before a compose: imports GPUCopy's back buffer and the scanout when
	gpu_compositor_scanout is on and they are new (the first frame, a mode
	change). True when the way changed, so the whole screen is damaged.
	The GPU is idle here: the last compose waited for its fence.
*/
bool
Compositor::_ScanoutPrepare(RenderingBuffer* back)
{
	CompositorVulkan* vk = fVulkan;
	if (!fWantScanout || fOnLavapipe || vk == NULL || !vk->canImport
		|| back == NULL)
		return false;
	uint32 current = GPUCopy::Generation();
	if (vk->scanActive && vk->scanGeneration == current)
		return false;
	if (!vk->scanActive && vk->scanFailedGeneration == current)
		return false;

	bool was = vk->scanActive;
	_ScanoutFree(vk);
	uint8 backToken[16];
	uint8 scanToken[16];
	uint32 scanPitch = 0;
	uint32 generation = 0;
	const char* reason = NULL;
	if (!GPUCopy::ExportForCompositor(back, backToken, scanToken, &scanPitch,
			&generation))
		reason = "back buffer not on the gpu";
	else if (!import_own(vk, backToken,
			(uint64)back->BytesPerRow() * back->Height(),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
				| VK_BUFFER_USAGE_TRANSFER_SRC_BIT
				| VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, &vk->back,
			&vk->backMemory)
		|| !import_own(vk, scanToken, (uint64)scanPitch * back->Height(),
			VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, &vk->scanout,
			&vk->scanoutMemory))
		reason = "import";
	if (reason != NULL) {
		_ScanoutFree(vk);
		vk->scanFailedGeneration = GPUCopy::Generation();
		if (!fScanoutLogged) {
			fScanoutLogged = true;
			log_line("scanout through vulkan off: %s", reason);
		}
		return was;
	}
	vk->backPitch = back->BytesPerRow();
	vk->scanPitch = scanPitch;
	vk->scanGeneration = generation;
	vk->scanActive = true;
	log_line("scanout through vulkan %" B_PRIu32 "x%" B_PRIu32
		" back pitch %" B_PRIu32 " scanout pitch %" B_PRIu32,
		back->Width(), back->Height(), vk->backPitch, vk->scanPitch);
	return true;
}


