/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "AomiPages.h"

#include "ServoContentView.h"

#include <Application.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <DataIO.h>
#include <Path.h>
#include <Roster.h>
#include <TranslationDefs.h>
#include <TranslatorRoster.h>
#include <WindowEffects.h>

#include <R5Icons.h>

#include <r5_os_version.h>

#include <stdio.h>
#include <stdlib.h>


static BString
html_escape(const char* s)
{
	BString out;
	if (s == NULL)
		return out;
	for (const char* p = s; *p != '\0'; p++) {
		if (*p == '&')
			out << "&amp;";
		else if (*p == '<')
			out << "&lt;";
		else if (*p == '>')
			out << "&gt;";
		else if (*p == '"')
			out << "&quot;";
		else {
			char c[2] = { *p, 0 };
			out << c;
		}
	}
	return out;
}


static BString
json_escape(const char* s)
{
	BString out("\"");
	if (s != NULL) {
		for (const unsigned char* p = (const unsigned char*)s; *p != 0; p++) {
			unsigned char c = *p;
			if (c == '"')
				out << "\\\"";
			else if (c == '\\')
				out << "\\\\";
			else if (c == '\n')
				out << "\\n";
			else if (c == '\r')
				out << "\\r";
			else if (c == '\t')
				out << "\\t";
			else if (c < 0x20) {
				char buf[8];
				snprintf(buf, sizeof(buf), "\\u%04x", c);
				out << buf;
			} else {
				char buf[2] = { (char)c, 0 };
				out << buf;
			}
		}
	}
	out << "\"";
	return out;
}


// The About page shows Aomi's own app icon (the HVIF in Aomi.rdef), as a
// PNG data URI, so the page and the Deskbar show the same mark. An empty
// string when it cannot be made; the page then keeps its plain badge.
static BString
aomi_icon_data_uri(float side)
{
	BString uri;
	app_info info;
	if (be_app == NULL || be_app->GetAppInfo(&info) != B_OK)
		return uri;
	BPath path(&info.ref);
	BBitmap* icon = r5_file_icon_bitmap(path.Path(), side);
	if (icon == NULL)
		return uri;
	BBitmapStream stream(icon);
	BMallocIO png;
	if (BTranslatorRoster::Default()->Translate(&stream, NULL, NULL, &png,
			B_PNG_FORMAT) != B_OK) {
		return uri;
	}
	static const char kTable[]
		= "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	const uint8* data = (const uint8*)png.Buffer();
	size_t size = png.BufferLength();
	uri = "data:image/png;base64,";
	for (size_t i = 0; i < size; i += 3) {
		uint32 chunk = (uint32)data[i] << 16;
		if (i + 1 < size)
			chunk |= (uint32)data[i + 1] << 8;
		if (i + 2 < size)
			chunk |= data[i + 2];
		char out[5] = {
			kTable[(chunk >> 18) & 63], kTable[(chunk >> 12) & 63],
			i + 1 < size ? kTable[(chunk >> 6) & 63] : '=',
			i + 2 < size ? kTable[chunk & 63] : '=', 0 };
		uri << out;
	}
	return uri;
}


BString
aomi_about_html(bool dark)
{
	BString html;
	html.SetToFormat(
		"<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
		"<title>About Aomi</title><style>"
		"body{margin:0;font:15px/1.6 sans-serif;background:%s;color:%s;}"
		".wrap{max-width:760px;margin:0 auto;padding:44px 28px 56px;}"
		".hero{background:linear-gradient(135deg,#0f2a4a 0%%,#194575 65%%,#3898ff 100%%);"
		"color:#fff;border-radius:14px;padding:34px 34px 30px;display:flex;align-items:center;gap:22px;}"
		".badge{width:72px;height:72px;border-radius:50%%;background:#0f2a4a;"
		"border:2px solid rgba(255,255,255,0.22);display:flex;align-items:center;justify-content:center;flex-shrink:0;}"
		".hero h1{margin:0 0 4px;font-size:28px;font-weight:800;}"
		".tag{display:inline-block;margin-left:8px;padding:2px 10px;font-size:12px;font-weight:700;"
		"border-radius:999px;background:rgba(255,255,255,0.18);vertical-align:middle;}"
		".hero p{margin:6px 0 0;color:rgba(255,255,255,0.88);}"
		".grid{display:grid;grid-template-columns:repeat(3,1fr);gap:14px;margin-top:20px;}"
		".card{background:%s;border:1px solid %s;border-radius:12px;padding:16px 18px;}"
		".label{font-size:11.5px;font-weight:700;text-transform:uppercase;letter-spacing:0.04em;color:%s;}"
		".val{margin-top:6px;font-size:16.5px;font-weight:700;}"
		".note{margin-top:4px;font-size:13px;color:%s;}"
		".sec{margin-top:20px;background:%s;border:1px solid %s;border-radius:12px;padding:20px 22px;}"
		".sec h2{margin:0 0 10px;font-size:16px;}"
		".sec p{margin:0 0 10px;color:%s;}"
		".acts{margin-top:14px;display:flex;gap:10px;flex-wrap:wrap;}"
		".btn{display:inline-block;padding:8px 16px;border-radius:8px;font-size:13.5px;font-weight:600;"
		"text-decoration:none;border:1px solid %s;color:%s;background:%s;}"
		".btnp{background:#3898ff;border-color:#3898ff;color:#fff;}"
		".foot{margin-top:22px;font-size:12.5px;color:%s;display:flex;justify-content:space-between;}"
		"</style></head><body><div class='wrap'>"
		"<section class='hero'>"
		"<div class='badge'><svg width='52' height='52' viewBox='0 0 64 64'>"
		"<circle cx='32' cy='32' r='28' fill='#0f2a4a'/>"
		"<path d='M8 38 C18 26,46 26,56 38 A28 28 0 0 1 8 38 Z' fill='#3898ff'/>"
		"<path d='M12 37 C22 29,42 29,52 37' fill='none' stroke='#fff' stroke-width='2.5' stroke-linecap='round'/>"
		"<circle cx='32' cy='21' r='4.5' fill='#fff'/>"
		"</svg></div>"
		"<div><h1>Aomi <span class='tag'>Experimental</span></h1>"
		"<p>The R5 Operating System browser. Browse beyond the horizon.</p></div>"
		"</section>"
		"<div class='grid'>"
		"<div class='card'><div class='label'>Operating System</div>"
		"<div class='val'>R5 %s</div><div class='note'>Build %d &middot; x86_64</div></div>"
		"<div class='card'><div class='label'>Web Engine</div>"
		"<div class='val'>Servo 0.8.0</div><div class='note'>SpiderMonkey 153 &middot; WebRender 0.70.1</div></div>"
		"<div class='card'><div class='label'>Graphics</div>"
		"<div class='val'>Zink on Vulkan</div><div class='note'>%s</div></div>"
		"</div>"
		"<section class='sec'><h2>Built for R5</h2>"
		"<p>Aomi is a native R5 browser window built on <code>BWebContentView</code> with Servo as its sole web engine. "
		"Bookmarks stay ordinary Tracker files in <code>WebPositive/Bookmarks</code>, and WebPositive remains installed as the default browser.</p>"
		"<p>Aomi is MIT licensed. Servo (<code>libr5servo.so</code>) is MPL-2.0 and permissive open-source licenses, with FreeType under the FreeType License (FTL).</p>"
		"<div class='acts'>"
		"<a class='btn btnp' href='aomi:technical'>Technical details</a>"
		"<a class='btn' href='aomi:technical.json'>Technical JSON</a>"
		"<a class='btn' href='aomi:newtab'>New tab</a>"
		"</div></section>"
		"<div class='foot'><span>&copy; 2026 R5OS Contributors</span><span>MIT &middot; Servo MPL-2.0</span></div>"
		"</div></body></html>",
		dark ? "#15181e" : "#f6f8fb",
		dark ? "#e6e9ef" : "#141923",
		dark ? "#1f232b" : "#ffffff",
		dark ? "#353b46" : "#d8dee8",
		dark ? "#a3abb8" : "#5b6270",
		dark ? "#a3abb8" : "#5b6270",
		dark ? "#1f232b" : "#ffffff",
		dark ? "#353b46" : "#d8dee8",
		dark ? "#a3abb8" : "#5b6270",
		dark ? "#353b46" : "#d8dee8",
		dark ? "#e6e9ef" : "#141923",
		dark ? "#15181e" : "#f6f8fb",
		dark ? "#a3abb8" : "#5b6270",
		R5_OS_VERSION,
		(int)R5_OS_BUILD,
		aomi_servo_gl_info()[0] != '\0'
			? html_escape(aomi_servo_gl_info()).String()
			: "RADV on AMD GPU &middot; Lavapipe fallback");
	BString icon = aomi_icon_data_uri(64);
	int32 start = html.FindFirst("<svg width='52'");
	int32 end = start >= 0 ? html.FindFirst("</svg>", start) : -1;
	if (icon.Length() > 0 && end > start) {
		BString image("<img width='64' height='64' alt='' src='");
		image << icon << "'>";
		html.Remove(start, end + 6 - start);
		html.Insert(image, start);
		html.ReplaceFirst(".badge{width:72px;height:72px;border-radius:50%;"
			"background:#0f2a4a;", ".badge{width:72px;height:72px;");
	}
	return html;
}


BString
aomi_technical_json()
{
	const char* driver = getenv("GALLIUM_DRIVER");
	if (driver == NULL || driver[0] == '\0')
		driver = "zink";
	bool glass = be_app != NULL && BAppearanceKit::window_effects_active();
	BString logs;
	aomi_log_dump_json(&logs);

	BString out;
	out << "{\n"
		<< "  \"aomi\": {\n"
		<< "    \"version\": \"1.0.0\",\n"
		<< "    \"channel\": \"experimental\",\n"
		<< "    \"r5_version\": " << json_escape(R5_OS_VERSION) << ",\n"
		<< "    \"r5_build\": " << (int)R5_OS_BUILD << ",\n"
		<< "    \"arch\": \"x86_64\"\n"
		<< "  },\n"
		<< "  \"engine\": {\n"
		<< "    \"name\": \"Servo\",\n"
		<< "    \"version\": \"0.8.0\",\n"
		<< "    \"js\": \"SpiderMonkey 153.6.0\",\n"
		<< "    \"compositor\": \"WebRender 0.70.1\",\n"
		<< "    \"library\": \"libr5servo.so\",\n"
		<< "    \"available\": " << (aomi_servo_available() ? "true" : "false") << ",\n"
		<< "    \"error\": " << json_escape(aomi_servo_error()) << "\n"
		<< "  },\n"
		<< "  \"network\": {\n"
		<< "    \"http2\": true,\n"
		<< "    \"last_protocol\": " << json_escape(aomi_servo_last_protocol()) << ",\n"
		<< "    \"stack\": \"hyper / rustls\"\n"
		<< "  },\n"
		<< "  \"graphics\": {\n"
		<< "    \"gl\": " << json_escape(aomi_servo_gl_info()) << ",\n"
		<< "    \"driver\": " << json_escape(driver) << ",\n"
		<< "    \"window_effects\": " << (glass ? "true" : "false") << "\n"
		<< "  },\n"
		<< "  \"recent_log\": " << logs << "\n"
		<< "}\n";
	return out;
}


BString
aomi_technical_text()
{
	const char* driver = getenv("GALLIUM_DRIVER");
	if (driver == NULL || driver[0] == '\0')
		driver = "zink";
	BString out;
	out.SetToFormat(
		"Aomi Technical Information\n"
		"==========================\n"
		"Aomi: 1.0.0 (experimental)\n"
		"R5 OS: %s (Build %d, x86_64)\n"
		"\n"
		"Engine\n"
		"------\n"
		"Name: Servo 0.8.0 (SpiderMonkey 153.6.0, WebRender 0.70.1)\n"
		"Library: libr5servo.so (%s)\n"
		"Error: %s\n"
		"\n"
		"Network\n"
		"-------\n"
		"Stack: hyper / rustls\n"
		"HTTP/2: enabled\n"
		"Last negotiated protocol: %s\n"
		"\n"
		"Graphics\n"
		"--------\n"
		"GL driver: %s\n"
		"GL renderer: %s\n"
		"Window effects (glass): %s\n"
		"\n"
		"Recent log\n"
		"----------\n"
		"%s",
		R5_OS_VERSION,
		(int)R5_OS_BUILD,
		aomi_servo_available() ? "loaded" : "unavailable",
		aomi_servo_error()[0] != '\0' ? aomi_servo_error() : "none",
		aomi_servo_last_protocol()[0] != '\0' ? aomi_servo_last_protocol() : "none yet",
		driver,
		aomi_servo_gl_info()[0] != '\0' ? aomi_servo_gl_info() : "not initialized yet",
		(be_app != NULL && BAppearanceKit::window_effects_active()) ? "active" : "flat (software)",
		aomi_log_dump_text().String());
	return out;
}


BString
aomi_technical_html(bool dark)
{
	const char* driver = getenv("GALLIUM_DRIVER");
	if (driver == NULL || driver[0] == '\0')
		driver = "zink";
	BString gl = aomi_servo_gl_info()[0] != '\0'
		? html_escape(aomi_servo_gl_info()) : BString("not initialized yet");
	BString proto = aomi_servo_last_protocol()[0] != '\0'
		? html_escape(aomi_servo_last_protocol()) : BString("none yet");
	BString logText = html_escape(aomi_log_dump_text().String());
	if (logText.Length() == 0)
		logText = "(no log entries yet)";

	BString html;
	html.SetToFormat(
		"<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
		"<title>Aomi Technical Information</title><style>"
		"body{margin:0;font:14px/1.55 sans-serif;background:%s;color:%s;}"
		".wrap{max-width:860px;margin:0 auto;padding:32px 24px 56px;}"
		".top{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:12px;margin-bottom:20px;}"
		"h1{margin:0;font-size:24px;}"
		".btn{display:inline-block;padding:7px 14px;border-radius:8px;font-size:13px;font-weight:600;"
		"text-decoration:none;border:1px solid %s;color:%s;background:%s;margin-left:8px;}"
		".btnp{background:#3898ff;border-color:#3898ff;color:#fff;}"
		".card{background:%s;border:1px solid %s;border-radius:10px;padding:16px 20px;margin-bottom:14px;}"
		"h2{margin:0 0 10px;font-size:15px;color:%s;text-transform:uppercase;letter-spacing:0.04em;}"
		"table{width:100%%;border-collapse:collapse;}"
		"td{padding:5px 8px;border-bottom:1px solid %s;vertical-align:top;}"
		"td.k{width:210px;font-weight:600;color:%s;}"
		"pre{margin:0;padding:12px;border-radius:8px;background:%s;font:12.5px/1.5 monospace;overflow:auto;white-space:pre-wrap;}"
		"</style></head><body><div class='wrap'>"
		"<div class='top'><h1>Aomi Technical Information</h1>"
		"<div><a class='btn btnp' href='aomi:copy-technical'>Copy as text</a>"
		"<a class='btn' href='aomi:technical.json'>View JSON</a></div></div>"
		"<div class='card'><h2>Versions</h2><table>"
		"<tr><td class='k'>Aomi</td><td>1.0.0 (experimental)</td></tr>"
		"<tr><td class='k'>R5 Operating System</td><td>%s (Build %d, x86_64)</td></tr>"
		"</table></div>"
		"<div class='card'><h2>Engine</h2><table>"
		"<tr><td class='k'>Web engine</td><td>Servo 0.8.0</td></tr>"
		"<tr><td class='k'>JavaScript</td><td>SpiderMonkey 153.6.0</td></tr>"
		"<tr><td class='k'>Compositor</td><td>WebRender 0.70.1</td></tr>"
		"<tr><td class='k'>Library</td><td>libr5servo.so (%s)</td></tr>"
		"</table></div>"
		"<div class='card'><h2>Network</h2><table>"
		"<tr><td class='k'>Stack</td><td>hyper / rustls</td></tr>"
		"<tr><td class='k'>HTTP/2</td><td>Enabled</td></tr>"
		"<tr><td class='k'>Last negotiated protocol</td><td>%s</td></tr>"
		"</table></div>"
		"<div class='card'><h2>Graphics</h2><table>"
		"<tr><td class='k'>GL driver</td><td>%s</td></tr>"
		"<tr><td class='k'>GL renderer</td><td>%s</td></tr>"
		"<tr><td class='k'>Window effects (glass)</td><td>%s</td></tr>"
		"</table></div>"
		"<div class='card'><h2>Recent log</h2><pre>%s</pre></div>"
		"</div></body></html>",
		dark ? "#15181e" : "#f6f8fb",
		dark ? "#e6e9ef" : "#141923",
		dark ? "#353b46" : "#d8dee8",
		dark ? "#e6e9ef" : "#141923",
		dark ? "#1f232b" : "#ffffff",
		dark ? "#1f232b" : "#ffffff",
		dark ? "#353b46" : "#d8dee8",
		dark ? "#a3abb8" : "#5b6270",
		dark ? "#2b303b" : "#eaedf2",
		dark ? "#a3abb8" : "#5b6270",
		dark ? "#111419" : "#f0f3f7",
		R5_OS_VERSION,
		(int)R5_OS_BUILD,
		aomi_servo_available() ? "loaded" : html_escape(aomi_servo_error()).String(),
		proto.String(),
		html_escape(driver).String(),
		gl.String(),
		(be_app != NULL && BAppearanceKit::window_effects_active()) ? "Active (GPU)" : "Flat (software)",
		logText.String());
	return html;
}
