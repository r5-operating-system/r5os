/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * M12 step 1 host test. Skia with Graphite on Vulkan draws one typical
 * desktop frame offscreen (docs/APP-SERVER-GPU-DRAWING.md, part 7): an
 * 800x600 window with a tab, 30 lines of text, 10 rectangles, a gradient,
 * two bitmaps and a row of round buttons. It prints the Vulkan devices
 * (API version, dualSrcBlend, VK_EXT_external_memory_host) and then
 *
 *	skia: <version> ok <device> frame <n> us
 *
 * where <n> is the median of 50 frames after 3 warm-up frames, each frame
 * drawn, snapped, inserted and submitted with a wait for the GPU.
 * Without an argument the CPU device (Lavapipe) is used; "--gpu" picks a
 * discrete or integrated GPU instead. Not part of the image.
 */

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkImage.h"
#include "include/core/SkMilestone.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"
#include "include/effects/SkGradient.h"
#include "include/gpu/graphite/Context.h"
#include "include/gpu/graphite/ContextOptions.h"
#include "include/gpu/graphite/GraphiteTypes.h"
#include "include/gpu/graphite/Image.h"
#include "include/gpu/graphite/Recorder.h"
#include "include/gpu/graphite/Recording.h"
#include "include/gpu/graphite/Surface.h"
#include "include/gpu/graphite/vk/VulkanGraphiteContext.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanExtensions.h"
#include "include/gpu/vk/VulkanMemoryAllocator.h"
#include "include/gpu/vk/VulkanPreferredFeatures.h"
#include "include/ports/SkFontMgr_empty.h"
#include "src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>


#ifndef R5_SKIA_VERSION
#	define R5_SKIA_VERSION "unknown"
#endif

static const int kWidth = 800;
static const int kHeight = 600;
static const int kWarmUp = 3;
static const int kFrames = 50;

static const SkColor kBackground = SkColorSetRGB(216, 216, 216);
static const SkColor kTab = SkColorSetRGB(255, 203, 0);


static int
fail(const char* reason)
{
	printf("skia: %s FAIL %s\n", R5_SKIA_VERSION, reason);
	return 1;
}


static const char*
device_type_name(VkPhysicalDeviceType type)
{
	switch (type) {
		case VK_PHYSICAL_DEVICE_TYPE_CPU:
			return "lavapipe";
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
			return "gpu";
		default:
			return "other";
	}
}


static bool
has_extension(const std::vector<VkExtensionProperties>& list, const char* name)
{
	for (const VkExtensionProperties& e : list) {
		if (strcmp(e.extensionName, name) == 0)
			return true;
	}
	return false;
}


static sk_sp<SkTypeface>
load_typeface(const sk_sp<SkFontMgr>& manager)
{
	static const char* kFonts[] = {
		"/boot/system/data/fonts/ttfonts/NotoSans-Regular.ttf",
		"/boot/system/data/fonts/ttfonts/manrope-regular.ttf",
		"/boot/system/data/fonts/ttfonts/DejaVuSans.ttf",
	};
	for (const char* path : kFonts) {
		if (access(path, R_OK) != 0)
			continue;
		sk_sp<SkTypeface> face = manager->makeFromFile(path);
		if (face) {
			printf("skia: font %s\n", path);
			return face;
		}
	}
	return nullptr;
}


static sk_sp<SkImage>
make_bitmap(int size, bool checker)
{
	SkBitmap bitmap;
	bitmap.allocN32Pixels(size, size);
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			SkColor c;
			if (checker) {
				c = ((x / 8 + y / 8) & 1) != 0
					? SkColorSetRGB(40, 90, 200) : SkColorSetRGB(240, 240, 240);
			} else {
				c = SkColorSetARGB(255 - y * 2, x * 4, 255 - x * 4, y * 4);
			}
			*bitmap.getAddr32(x, y) = SkPreMultiplyColor(c);
		}
	}
	bitmap.setImmutable();
	return bitmap.asImage();
}


struct Frame {
	sk_sp<SkTypeface>	face;
	sk_sp<SkImage>		bitmap1;
	sk_sp<SkImage>		bitmap2;
};


static void
draw_frame(SkCanvas* canvas, const Frame& frame, int n)
{
	canvas->clear(kBackground);

	// The window: a body with a frame line, and a tab with its title.
	SkPaint paint;
	paint.setAntiAlias(true);
	paint.setColor(SK_ColorWHITE);
	canvas->drawRect(SkRect::MakeXYWH(4, 26, 792, 570), paint);
	paint.setStyle(SkPaint::kStroke_Style);
	paint.setStrokeWidth(1);
	paint.setColor(SkColorSetRGB(150, 150, 150));
	canvas->drawRect(SkRect::MakeXYWH(3.5f, 25.5f, 793, 571), paint);
	paint.setStyle(SkPaint::kFill_Style);
	paint.setColor(kTab);
	canvas->drawRect(SkRect::MakeXYWH(0, 0, 220, 26), paint);

	SkFont font(frame.face, 12);
	font.setEdging(SkFont::Edging::kAntiAlias);
	SkPaint ink;
	ink.setAntiAlias(true);
	ink.setColor(SK_ColorBLACK);
	canvas->drawString("Skia frame", 26, 18, font, ink);

	// 30 lines of text.
	for (int i = 0; i < 30; i++) {
		std::string line = "Line " + std::to_string(i + 1)
			+ ": The quick brown fox jumps over the lazy dog, frame "
			+ std::to_string(n);
		canvas->drawString(line.c_str(), 16, 50 + i * 15, font, ink);
	}

	// 10 rectangles.
	for (int i = 0; i < 10; i++) {
		paint.setColor(SkColorSetRGB(30 + i * 20, 120, 220 - i * 15));
		canvas->drawRect(SkRect::MakeXYWH(470 + (i % 5) * 62, 40 + (i / 5) * 62,
			54, 54), paint);
	}

	// A gradient.
	const SkColor4f colors[] = {
		SkColor4f::FromColor(SkColorSetRGB(255, 255, 255)),
		SkColor4f::FromColor(SkColorSetRGB(255, 203, 0)),
		SkColor4f::FromColor(SkColorSetRGB(200, 60, 20)),
	};
	const SkPoint points[] = { {470, 180}, {780, 180} };
	SkGradient gradient(SkGradient::Colors(SkSpan(colors, 3), SkTileMode::kClamp),
		SkGradient::Interpolation());
	SkPaint shaded;
	shaded.setShader(SkShaders::LinearGradient(points, gradient));
	canvas->drawRect(SkRect::MakeXYWH(470, 170, 310, 60), shaded);

	// Two bitmaps: one at its size, one scaled with filtering.
	canvas->drawImage(frame.bitmap1, 470, 250);
	canvas->drawImageRect(frame.bitmap2, SkRect::MakeXYWH(560, 250, 128, 128),
		SkSamplingOptions(SkFilterMode::kLinear));

	// A row of round buttons, as in a dialog.
	for (int i = 0; i < 6; i++) {
		SkRect r = SkRect::MakeXYWH(16 + i * 112, 520, 100, 26);
		SkRRect rr = SkRRect::MakeRectXY(r, 5, 5);
		paint.setStyle(SkPaint::kFill_Style);
		paint.setColor(SkColorSetRGB(235, 235, 235));
		canvas->drawRRect(rr, paint);
		paint.setStyle(SkPaint::kStroke_Style);
		paint.setColor(SkColorSetRGB(120, 120, 120));
		canvas->drawRRect(rr, paint);
		paint.setStyle(SkPaint::kFill_Style);
		std::string label = "Button " + std::to_string(i + 1);
		canvas->drawString(label.c_str(), r.x() + 26, r.y() + 17, font, ink);
	}
}


struct ReadBack {
	bool			done = false;
	std::vector<uint32_t> pixels;
};


static void
read_done(SkImage::ReadPixelsContext context,
	std::unique_ptr<const SkImage::AsyncReadResult> result)
{
	ReadBack* readBack = static_cast<ReadBack*>(context);
	readBack->done = true;
	if (!result)
		return;
	const uint8_t* data = static_cast<const uint8_t*>(result->data(0));
	size_t rowBytes = result->rowBytes(0);
	readBack->pixels.resize(kWidth * kHeight);
	for (int y = 0; y < kHeight; y++)
		memcpy(&readBack->pixels[y * kWidth], data + y * rowBytes, kWidth * 4);
}


static bool
near(uint32_t pixel, SkColor expected)
{
	// The read back is RGBA8888: R in the low byte.
	int r = pixel & 0xff, g = (pixel >> 8) & 0xff, b = (pixel >> 16) & 0xff;
	return abs(r - (int)SkColorGetR(expected)) <= 2
		&& abs(g - (int)SkColorGetG(expected)) <= 2
		&& abs(b - (int)SkColorGetB(expected)) <= 2;
}


int
main(int argc, char** argv)
{
	bool wantGpu = argc > 1 && strcmp(argv[1], "--gpu") == 0;
	const uint32_t apiVersion = VK_API_VERSION_1_1;

	VkApplicationInfo app = {};
	app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app.pApplicationName = "skia-frame";
	app.apiVersion = apiVersion;

	skgpu::VulkanPreferredFeatures preferred;
	preferred.init(apiVersion);

	uint32_t count = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
	std::vector<VkExtensionProperties> instanceAvailable(count);
	vkEnumerateInstanceExtensionProperties(nullptr, &count, instanceAvailable.data());
	std::vector<const char*> instanceExtensions;
	preferred.addToInstanceExtensions(instanceAvailable.data(), instanceAvailable.size(),
		instanceExtensions);

	VkInstanceCreateInfo instanceInfo = {};
	instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.pApplicationInfo = &app;
	instanceInfo.enabledExtensionCount = instanceExtensions.size();
	instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();
	VkInstance instance;
	if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS)
		return fail("vkCreateInstance");

	count = 0;
	vkEnumeratePhysicalDevices(instance, &count, nullptr);
	std::vector<VkPhysicalDevice> devices(count);
	vkEnumeratePhysicalDevices(instance, &count, devices.data());

	// Step 1, point 3: what each device reports, for later steps.
	VkPhysicalDevice chosen = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties chosenProperties = {};
	for (VkPhysicalDevice device : devices) {
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(device, &properties);
		VkPhysicalDeviceFeatures features;
		vkGetPhysicalDeviceFeatures(device, &features);
		uint32_t n = 0;
		vkEnumerateDeviceExtensionProperties(device, nullptr, &n, nullptr);
		std::vector<VkExtensionProperties> extensions(n);
		vkEnumerateDeviceExtensionProperties(device, nullptr, &n, extensions.data());
		printf("skia-vk: %s type %s api %u.%u.%u dualSrcBlend %d"
			" external_memory_host %s\n", properties.deviceName,
			device_type_name(properties.deviceType),
			VK_API_VERSION_MAJOR(properties.apiVersion),
			VK_API_VERSION_MINOR(properties.apiVersion),
			VK_API_VERSION_PATCH(properties.apiVersion), features.dualSrcBlend,
			has_extension(extensions, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME)
				? "yes" : "no");
		bool isGpu = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
			|| properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
		bool isCpu = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
		if (chosen == VK_NULL_HANDLE && (wantGpu ? isGpu : isCpu)) {
			chosen = device;
			chosenProperties = properties;
		}
	}
	if (chosen == VK_NULL_HANDLE)
		return fail(wantGpu ? "no gpu device" : "no lavapipe device");

	count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(chosen, &count, nullptr);
	std::vector<VkQueueFamilyProperties> families(count);
	vkGetPhysicalDeviceQueueFamilyProperties(chosen, &count, families.data());
	uint32_t queueFamily = UINT32_MAX;
	for (uint32_t i = 0; i < count; i++) {
		if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
			queueFamily = i;
			break;
		}
	}
	if (queueFamily == UINT32_MAX)
		return fail("no graphics queue");

	count = 0;
	vkEnumerateDeviceExtensionProperties(chosen, nullptr, &count, nullptr);
	std::vector<VkExtensionProperties> deviceAvailable(count);
	vkEnumerateDeviceExtensionProperties(chosen, nullptr, &count, deviceAvailable.data());

	// Skia adds what it prefers; RADV and Lavapipe each give what they have.
	VkPhysicalDeviceFeatures2 features2 = {};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	preferred.addFeaturesToQuery(deviceAvailable.data(), deviceAvailable.size(), features2);
	vkGetPhysicalDeviceFeatures2(chosen, &features2);
	std::vector<const char*> deviceExtensions;
	preferred.addFeaturesToEnable(deviceExtensions, features2);

	float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = {};
	queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueInfo.queueFamilyIndex = queueFamily;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;

	VkDeviceCreateInfo deviceInfo = {};
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.pNext = &features2;
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	deviceInfo.enabledExtensionCount = deviceExtensions.size();
	deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
	VkDevice device;
	if (vkCreateDevice(chosen, &deviceInfo, nullptr, &device) != VK_SUCCESS)
		return fail("vkCreateDevice");
	VkQueue queue;
	vkGetDeviceQueue(device, queueFamily, 0, &queue);

	skgpu::VulkanExtensions extensions;
	skgpu::VulkanGetProc getProc = [](const char* name, VkInstance i, VkDevice d) {
		if (d != VK_NULL_HANDLE)
			return vkGetDeviceProcAddr(d, name);
		return vkGetInstanceProcAddr(i, name);
	};
	extensions.init(getProc, instance, chosen, instanceExtensions.size(),
		instanceExtensions.data(), deviceExtensions.size(), deviceExtensions.data());

	skgpu::VulkanBackendContext backend;
	backend.fInstance = instance;
	backend.fPhysicalDevice = chosen;
	backend.fDevice = device;
	backend.fQueue = queue;
	backend.fGraphicsQueueIndex = queueFamily;
	backend.fMaxAPIVersion = apiVersion;
	backend.fVkExtensions = &extensions;
	backend.fDeviceFeatures2 = &features2;
	backend.fGetProc = getProc;
	// ThreadSafe is only forward declared (bool) in the header that is
	// packed; its values live in another private header. One thread here.
	backend.fMemoryAllocator = skgpu::VulkanMemoryAllocators::Make(backend,
		static_cast<skgpu::ThreadSafe>(false));
	if (!backend.fMemoryAllocator)
		return fail("no memory allocator");

	skgpu::graphite::ContextOptions options;
	std::unique_ptr<skgpu::graphite::Context> context
		= skgpu::graphite::ContextFactory::MakeVulkan(backend, options);
	if (!context)
		return fail("no graphite context");
	std::unique_ptr<skgpu::graphite::Recorder> recorder = context->makeRecorder();
	if (!recorder)
		return fail("no recorder");

	SkImageInfo info = SkImageInfo::Make(kWidth, kHeight, kBGRA_8888_SkColorType,
		kPremul_SkAlphaType);
	sk_sp<SkSurface> surface = SkSurfaces::RenderTarget(recorder.get(), info);
	if (!surface)
		return fail("no surface");

	Frame frame;
	frame.face = load_typeface(SkFontMgr_New_Custom_Empty());
	if (!frame.face)
		return fail("no font");
	frame.bitmap1 = SkImages::TextureFromImage(recorder.get(), make_bitmap(64, true));
	frame.bitmap2 = SkImages::TextureFromImage(recorder.get(), make_bitmap(64, false));
	if (!frame.bitmap1 || !frame.bitmap2)
		return fail("no bitmap texture");

	std::vector<long> times;
	long first = 0;
	for (int n = 0; n < kWarmUp + kFrames; n++) {
		auto start = std::chrono::steady_clock::now();
		draw_frame(surface->getCanvas(), frame, n);
		std::unique_ptr<skgpu::graphite::Recording> recording = recorder->snap();
		if (!recording)
			return fail("snap");
		skgpu::graphite::InsertRecordingInfo insert;
		insert.fRecording = recording.get();
		if (context->insertRecording(insert) != skgpu::graphite::InsertStatus::kSuccess)
			return fail("insertRecording");
		skgpu::graphite::SubmitInfo submit;
		submit.fSync = skgpu::graphite::SyncToCpu::kYes;
		if (!context->submit(submit))
			return fail("submit");
		long us = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - start).count();
		if (n == 0)
			first = us;
		if (n >= kWarmUp)
			times.push_back(us);
		if (context->isDeviceLost())
			return fail("device lost");
	}

	// Read the last frame back and check a few pixels.
	ReadBack readBack;
	SkImageInfo readInfo = SkImageInfo::Make(kWidth, kHeight, kRGBA_8888_SkColorType,
		kPremul_SkAlphaType);
	context->asyncRescaleAndReadPixels(surface.get(), readInfo,
		SkIRect::MakeWH(kWidth, kHeight), SkImage::RescaleGamma::kSrc,
		SkImage::RescaleMode::kNearest, read_done, &readBack);
	skgpu::graphite::SubmitInfo submit;
	submit.fSync = skgpu::graphite::SyncToCpu::kYes;
	context->submit(submit);
	for (int i = 0; i < 100 && !readBack.done; i++) {
		context->checkAsyncWorkCompletion();
		usleep(10000);
	}
	if (readBack.pixels.empty())
		return fail("read back");
	const std::vector<uint32_t>& p = readBack.pixels;
	if (!near(p[2 * kWidth + 200], kTab))
		return fail("tab pixel");
	if (!near(p[598 * kWidth + 799], kBackground))
		return fail("background pixel");
	if (!near(p[60 * kWidth + 490], SkColorSetRGB(30, 120, 220)))
		return fail("rectangle pixel");
	if (!near(p[100 * kWidth + 600], SK_ColorWHITE))
		return fail("body pixel");

	std::sort(times.begin(), times.end());
	long median = times[times.size() / 2];
	printf("skia: device %s, first frame %ld us, min %ld us, max %ld us\n",
		chosenProperties.deviceName, first, times.front(), times.back());
	printf("skia: %s ok %s frame %ld us\n", R5_SKIA_VERSION,
		device_type_name(chosenProperties.deviceType), median);

	surface.reset();
	frame = Frame();
	recorder.reset();
	context.reset();
	backend.fMemoryAllocator.reset();
	vkDestroyDevice(device, nullptr);
	vkDestroyInstance(instance, nullptr);
	return 0;
}
