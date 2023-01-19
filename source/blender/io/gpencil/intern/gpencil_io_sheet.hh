/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2020 Blender Foundation. All rights reserved. */
#pragma once

/** \file
 * \ingroup bgpencil
 */
#include "BLI_math_vector_types.hh"

#include "DNA_space_types.h" /* for FILE_MAX */

#include "hpdf.h"

struct ContactSheetParams;

namespace blender::io::gpencil {

class ContactSheetPDF {

 public:
  /** Total pages. */
  uint32_t totpages_;

  /** Items by page. */
  uint32_t bypage_;

  ContactSheetPDF(struct bContext *C, struct ContactSheetParams *iparams);
  bool create_document();
  bool add_newpage(const uint32_t pagenum);
  bool save_document();
  void free_document();

 protected:
  ContactSheetParams params_;
  /* Data for easy access. */
  struct Main *bmain_;
  struct Scene *scene_;

 private:
  /** PDF document. */
  HPDF_Doc pdf_;
  /** PDF page. */
  HPDF_Page page_;
  /** Default Font. */
  HPDF_Font font_;
  /** Output PDF path. */
  char filepath_[FILE_MAX];

  /** Output PDF size. */
  float2 page_size_;

  /** Available canvas size. */
  float2 canvas_size_;

  /** Design of page. */
  uint32_t rows_, cols_;

  /** Thumbnail size. */
  float2 thumb_size_;

  /** Gap size between images. */
  float2 gap_size_;
  /** Thumbnail offset. */
  float2 offset_;
  /** Creation date and time in text format. */
  char date_creation_[128];

  /** Get size of thumbnail relative to page canvas size. */
  void get_thumbnail_size(int iw, int ih);
  /** Add a text to the pdf. */
  void write_text(float2 loc, const char *text);
  /** Draw main page frame. */
  void draw_page_frame(uint32_t pagenum);
  /** Draw image to the canvas. */
  void draw_thumbnail(HPDF_Image pdf_image, int row, int col, ContactSheetItem *item);
  /** Load image and create thumbnail. */
  void load_and_draw_image(ContactSheetItem *item, const int row, const int col);
  /** Load logo image and draw in page. */
  void load_and_draw_logo(void);
};

}  // namespace blender::io::gpencil
