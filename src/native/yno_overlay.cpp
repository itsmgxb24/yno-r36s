/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "yno_overlay.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "../bitmap.h"
#include "../drawable.h"
#include "../drawable_mgr.h"
#include "../font.h"
#include "../text.h"
#include "yno_session.h"

namespace {
	constexpr int kLines = 6;           // visible chat lines
	constexpr int kLineHeight = 12;
	constexpr int kShowFrames = 60 * 9; // a new message stays visible for ~9 s
	constexpr int kFadeFrames = 40;
	constexpr int kPadding = 3;

	bool pinned = false;

	// the byte length of the UTF-8 sequence that starts with `lead`
	size_t Utf8Len(unsigned char lead) {
		if (lead < 0x80) return 1;
		if ((lead >> 5) == 0x6) return 2;
		if ((lead >> 4) == 0xE) return 3;
		if ((lead >> 3) == 0x1E) return 4;
		return 1;
	}

	std::vector<std::string> Wrap(const Font& font, const std::string& text, int width, const std::string& indent) {
		std::vector<std::string> lines;
		std::string line;
		size_t i = 0;
		while (i < text.size()) {
			size_t n = std::min(Utf8Len(static_cast<unsigned char>(text[i])), text.size() - i);
			std::string next = line + text.substr(i, n);
			if (!line.empty() && Text::GetSize(font, next).width > width) {
				lines.push_back(line);
				line = indent;
				if (text[i] == ' ') {   // do not start the continuation line with the break space
					i += n;
					continue;
				}
				next = line + text.substr(i, n);
			}
			line = std::move(next);
			i += n;
		}
		if (!line.empty()) lines.push_back(line);
		return lines;
	}

	class Overlay final : public Drawable {
	public:
		Overlay() : Drawable(Priority_Overlay + 10, Drawable::Flags::Global) {
			DrawableMgr::Register(this);
		}

		void Draw(Bitmap& dst) override {
			if (YnoSession::ChatVersion() != seen_version) {
				seen_version = YnoSession::ChatVersion();
				Rebuild(dst.width());
				frames_left = kShowFrames;
			}
			if (!image) return;

			int opacity = 255;
			if (!pinned) {
				if (frames_left <= 0) return;
				--frames_left;
				if (frames_left < kFadeFrames) opacity = 255 * frames_left / kFadeFrames;
			}
			dst.Blit(0, dst.height() - image->height(), *image, image->GetRect(), Opacity(opacity));
		}

	private:
		void Rebuild(int width) {
			FontRef font = Font::DefaultBitmapFont();
			std::vector<std::string> lines;
			const auto& chat = YnoSession::Chat();
			// walk back from the newest message until the visible lines are full
			std::vector<std::vector<std::string>> blocks;
			size_t used = 0;
			for (auto it = chat.rbegin(); it != chat.rend() && used < kLines; ++it) {
				std::string prefix = it->type == YnoSession::ChatType::Global ? "[G] " : it->type == YnoSession::ChatType::Party ? "[P] " : "";
				auto block = Wrap(*font, prefix + it->name + ": " + it->text, width - 2 * kPadding, "  ");
				used += block.size();
				blocks.push_back(std::move(block));
			}
			for (auto it = blocks.rbegin(); it != blocks.rend(); ++it) {
				lines.insert(lines.end(), it->begin(), it->end());
			}
			if (lines.size() > kLines) {
				lines.erase(lines.begin(), lines.end() - kLines);
			}
			if (lines.empty()) {
				image.reset();
				return;
			}
			image = Bitmap::Create(width, static_cast<int>(lines.size()) * kLineHeight + 2 * kPadding - 2, true);
			image->FillRect(image->GetRect(), Color(0, 0, 0, 150));
			for (size_t i = 0; i < lines.size(); ++i) {
				int y = kPadding - 1 + static_cast<int>(i) * kLineHeight;
				Text::Draw(*image, kPadding + 1, y + 1, *font, Color(0, 0, 0, 255), lines[i]);
				Text::Draw(*image, kPadding, y, *font, Color(255, 255, 255, 255), lines[i]);
			}
		}

		BitmapRef image;
		uint32_t seen_version = 0;
		int frames_left = 0;
	};

	std::unique_ptr<Overlay> overlay;
}

namespace YnoOverlay {

void Init() {
	if (getenv("YNO_CHAT_PINNED")) pinned = true;
	if (!overlay) overlay = std::make_unique<Overlay>();
}

void Shutdown() {
	overlay.reset();
}

void SetChatPinned(bool p) {
	pinned = p;
}

bool IsChatPinned() {
	return pinned;
}

}
