#include "pdf_extractor.hpp"

#include <mupdf/fitz.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

static void append_utf8(std::string& s, int c) {
  if (c < 0x80) {
    s += static_cast<char>(c);
  } else if (c < 0x800) {
    s += static_cast<char>(0xC0 | (c >> 6));
    s += static_cast<char>(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    s += static_cast<char>(0xE0 | (c >> 12));
    s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (c & 0x3F));
  } else {
    s += static_cast<char>(0xF0 | (c >> 18));
    s += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
    s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (c & 0x3F));
  }
}

struct BlkData {
  std::string text;
  fz_rect bbox;
  float sz;
};

std::vector<std::string> extract_pages(const std::string& path) {
  fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
  if (!ctx) throw std::runtime_error("failed to create MuPDF context");

  fz_register_document_handlers(ctx);

  std::vector<std::string> pages;
  fz_document* doc = nullptr;

  fz_try(ctx) {
    doc = fz_open_document(ctx, path.c_str());
    int count = fz_count_pages(ctx, doc);

    fz_stext_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.flags = FZ_STEXT_PRESERVE_WHITESPACE;

    for (int i = 0; i < count; ++i) {
      fz_stext_page* pg =
          fz_new_stext_page_from_page_number(ctx, doc, i, &opts);

      std::vector<BlkData> blks;
      for (fz_stext_block* blk = pg->first_block; blk; blk = blk->next) {
        if (blk->type != FZ_STEXT_BLOCK_TEXT) continue;
        BlkData bd;
        bd.bbox = blk->bbox;
        bd.sz = 12.0f;
        bool got_sz = false;
        for (fz_stext_line* ln = blk->u.t.first_line; ln; ln = ln->next) {
          for (fz_stext_char* ch = ln->first_char; ch; ch = ch->next) {
            if (!got_sz && ch->size > 0) {
              bd.sz = ch->size;
              got_sz = true;
            }
            append_utf8(bd.text, ch->c);
          }
          bd.text += '\n';
        }
        if (!bd.text.empty()) blks.push_back(std::move(bd));
      }

      std::string page_text;
      for (size_t j = 0; j < blks.size(); ++j) {
        page_text += blks[j].text;
        if (j + 1 < blks.size()) {
          const std::string& nt = blks[j + 1].text;
          bool next_indented = false;
          if (nt.size() >= 3) {
            auto b0 = static_cast<unsigned char>(nt[0]);
            auto b1 = static_cast<unsigned char>(nt[1]);
            auto b2 = static_cast<unsigned char>(nt[2]);
            next_indented =
                (b0 == 0xE3 && b1 == 0x80 && b2 == 0x80) ||  // U+3000
                (b0 == 0xE2 && b1 == 0x80 && b2 == 0x83);    // U+2003
          }
          float gap = blks[j + 1].bbox.y0 - blks[j].bbox.y1;
          // Add paragraph break when there is an indent character/large
          // vertical gap.
          if (next_indented || gap > blks[j].sz * 0.5f) page_text += '\n';
        } else {
          page_text += '\n';  // ensure page ends with \n\n
        }
      }

      fz_drop_stext_page(ctx, pg);
      pages.push_back(std::move(page_text));
    }
  }
  fz_always(ctx) { fz_drop_document(ctx, doc); }
  fz_catch(ctx) {
    std::string msg = fz_caught_message(ctx);
    fz_drop_context(ctx);
    throw std::runtime_error(msg);
  }

  fz_drop_context(ctx);
  return pages;
}
