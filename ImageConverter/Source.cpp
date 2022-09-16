#include <allegro5/allegro.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_native_dialog.h>
#include <iostream>
#include <thread>
#include <string>
#include <algorithm>
#include <vector>
#include <filesystem>

const std::string avail_formats[] = {"png","jpg","bmp"};

void async_ev(bool& keep, ALLEGRO_EVENT_QUEUE* evqu);
const char* static_sprint(float);
std::string autolist(const std::vector<std::string>&);

int main(int argc, char* argv[])
{
	const bool is_custom = (argc >= 2 && strcmp(argv[1], "-custom") == 0); // flag for general, checked l8 for flags
	bool search_all = false;

	if (argc == 1) {
		int res = al_show_native_message_box(nullptr, "Search for files in this directory and convert to JPEG?", "Hit YES if you want to automatically convert all files around here as jpg @ 80% quality", "", nullptr, ALLEGRO_MESSAGEBOX_YES_NO);
		if (res != 1) return 0;
		search_all = true;
	}
	else if (is_custom && argc < 5) {
		al_show_native_message_box(nullptr, "Insufficient arguments", "Please follow the format:", "<app.exe> [-custom <FORMAT(extension)> <QUALITY(float,for jpg)>] <files...>\n\nThe files will be saved as conv_*.", nullptr, 0);
		return 0; // no task
	}
	// if is_custom, argc is >= 5 from here

	search_all |= (is_custom && strcmp(argv[4], "*") == 0);
	std::string format = is_custom ? argv[2] : "jpg";
	float quality = is_custom ? std::atof(argv[3]) : 0.80f;
	bool keep = true;
	std::vector<std::string> args;
	std::vector<std::string> fail_load, fail_save, fail_select;


	std::transform(format.begin(), format.end(), format.begin(), [](char c) {return std::tolower(c); });

	al_init();
	al_init_image_addon();
	al_init_font_addon();

	al_set_app_name("ImageConverter");
	al_set_new_window_title(("ImageConverter by Lohk 2022 - " + (format == "jpg" ? (std::string(static_sprint(quality)) + "% JPG") : format)).c_str());

	if (search_all) {
		// get all files

		for (const auto& i : std::filesystem::directory_iterator(".")) {
			if (i.is_regular_file() && i.file_size() > 0) {
				std::string pt = i.path().string();

				while (pt.size() && pt.front() == '.') pt.erase(pt.begin());  // remove path thingy
				while (pt.size() && pt.front() == '\\') pt.erase(pt.begin()); // remove path thingy
				while (pt.size() && pt.front() == '/') pt.erase(pt.begin());  // remove path thingy

				if (pt.find("conv_") == 0) continue;
				if (al_identify_bitmap(pt.c_str())) args.push_back(pt);
				else fail_select.push_back(pt);
			}
		}
	}
	else {
		for (int a = is_custom ? 4 : 1; a < argc && keep; ++a) {
			if (al_identify_bitmap(argv[a])) args.push_back(argv[a]);
			else fail_select.push_back(argv[a]);
		}
	}

	// Check if has input or error in input

	if (args.empty()) {
		al_show_native_message_box(nullptr, "Could not open a single file!", "Failed opening any of the files in input.", "", nullptr, ALLEGRO_MESSAGEBOX_WARN);
		return 0;
	}
	else if (fail_select.size()) {
		auto str = autolist(fail_select) + "\n\nThe app will continue with the other items after this message.";
		al_show_native_message_box(nullptr, "File list had issues", "Some items were not possible to add to the list:", str.c_str(), nullptr, ALLEGRO_MESSAGEBOX_WARN);
	}

	al_set_new_display_flags(ALLEGRO_OPENGL);
	al_set_new_bitmap_format(ALLEGRO_PIXEL_FORMAT_ARGB_8888);
	al_set_new_bitmap_flags(ALLEGRO_VIDEO_BITMAP | ALLEGRO_NO_PREMULTIPLIED_ALPHA | ALLEGRO_MIN_LINEAR);

	ALLEGRO_MONITOR_INFO mi;
	al_get_monitor_info(0, &mi);

	auto disp = al_create_display((mi.x2 - mi.x1) * 0.4f, (mi.y2 - mi.y1) * 0.4f);
	auto font = al_create_builtin_font();
	auto evqu = al_create_event_queue();
	const float scrx = (mi.x2 - mi.x1) * 1.0f / (mi.y2 - mi.y1);
	bool had_issues = false;

	al_register_event_source(evqu, al_get_display_event_source(disp));

	auto thr = std::thread([&] { async_ev(keep, evqu); });

	const auto autodel = [&] {
		keep = false;
		thr.join();
		al_destroy_event_queue(evqu);
		al_destroy_font(font);
		al_destroy_display(disp);
	};

	if (std::find(std::begin(avail_formats), std::end(avail_formats), format) == std::end(avail_formats)) {
		std::string buil = "Try one of the following:\n";
		for (const auto& i : avail_formats) buil += (i + "\n");
		buil.pop_back();
		al_show_native_message_box(disp, "Call malformed", "The parameter \"FORMAT\" is not supported.", buil.c_str(), nullptr, 0);
		autodel();
		return 0;
	}
	if (quality <= 0.0f || quality > 1.0f) {
		al_show_native_message_box(disp, "Call malformed", "The parameter \"QUALITY\" is not supported.", "Please input a value bigger than 0.0 and at most 1.0 next time", nullptr, 0);
		autodel();
		return 0;
	}

	al_set_config_value(al_get_system_config(), "image", "jpeg_quality_level", static_sprint(quality));	// https://github.com/liballeg/allegro5/blob/master/allegro5.cfg


	for (size_t c = 0; c < args.size(); ++c) {
		auto& i = args[c];
		al_clear_to_color(al_map_rgb(0, 0, 0));
		ALLEGRO_BITMAP* bmp = al_load_bitmap(i.c_str());
		if (!bmp) {
			fail_load.push_back(i);
			//al_show_native_message_box(disp, "Failed loading one file", "It looks like this item is not supported, ok?", i.c_str(), nullptr, ALLEGRO_MESSAGEBOX_WARN);
			//had_issues = true;
			continue;
		}		
		const float xsca = al_get_bitmap_width(bmp) * 1.0f / al_get_bitmap_height(bmp);

		if (scrx > xsca) { // screen is wider in X than bitmap, zoom from top
			const float dx = al_get_display_width(disp);
			const float dy = al_get_display_width(disp) * 1.0f / xsca;
			al_draw_scaled_bitmap(bmp, 0, 0, al_get_bitmap_width(bmp), al_get_bitmap_height(bmp), 0, -0.5f * (dy - al_get_display_height(disp)), dx, dy, 0);
		}
		else { // screen is less wide in X than bitmap, zoom from sides
			const float dx = al_get_display_height(disp) * xsca;
			const float dy = al_get_display_height(disp);
			al_draw_scaled_bitmap(bmp, 0, 0, al_get_bitmap_width(bmp), al_get_bitmap_height(bmp), -0.5f * (dx - al_get_display_width(disp)), 0, dx, dy, 0);
		}
		al_draw_textf(font, al_map_rgb(0, 0, 0), 4, 4, 0, "[%zu/%zu] %s", c + 1, args.size(), i.c_str());
		al_draw_textf(font, al_map_rgb(255, 255, 255), 3, 3, 0, "[%zu/%zu] %s", c + 1, args.size(), i.c_str());

		al_flip_display();

		std::string out = "conv_" + i + "." + format;

		if (!al_save_bitmap(out.c_str(), bmp)) {
			fail_save.push_back(i);
			//al_show_native_message_box(disp, "Failed saving one file", "Something went wrong and I failed to save this file.", out.c_str(), nullptr, ALLEGRO_MESSAGEBOX_WARN);
			//had_issues = true;
		}

		al_destroy_bitmap(bmp);

		//al_rest(10.0);
	}

	autodel();

	if (fail_load.size()) al_show_native_message_box(nullptr, "Something went wrong with these items", "These files failed to open, so they were skipped:", autolist(fail_load).c_str(), nullptr, 0);
	if (fail_save.size()) al_show_native_message_box(nullptr, "Something went wrong with these items", "These files refused to be saved in disk:", autolist(fail_save).c_str(), nullptr, 0);

	if (fail_load.empty() && fail_save.empty()) al_show_native_message_box(nullptr, "Completed", "Completed task list successfully.", "No issues found.", nullptr, 0);
}

void async_ev(bool& keep, ALLEGRO_EVENT_QUEUE* evqu)
{
	while (keep) {
		ALLEGRO_EVENT ev;
		if (!al_wait_for_event_timed(evqu, &ev, 0.1f)) continue;

		switch (ev.type) {
		case ALLEGRO_EVENT_DISPLAY_CLOSE:
			keep = false;
			break;
		}
	}
}

const char* static_sprint(float f)
{
	static char b[64];
	snprintf(b, std::size(b), "%.0f", 100.0f * f);
	return b;
}

std::string autolist(const std::vector<std::string>& vec)
{
	std::string a;
	for (size_t p = 0; p < vec.size(); ++p) {
		a += vec[p] + "\n";
	}
	if (a.size()) a.pop_back();
	return a;
}