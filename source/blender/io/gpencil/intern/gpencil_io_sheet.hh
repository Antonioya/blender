/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2020 Blender Foundation. All rights reserved. */
#pragma once

/** \file
 * \ingroup bgpencil
 */
#include "BLI_math_vec_types.hh"

#include "DNA_space_types.h" /* for FILE_MAX */

#include "hpdf.h"

struct ContactSheetParams;

#define PDF_SHEET_EXPORTER_NAME "PDF Contact Sheet"
#define PDF_SHEET_EXPORTER_VERSION "v1.0"

namespace blender::io::gpencil {

class ContactSheetPDF {

 public:
  ContactSheetPDF(struct bContext *C, struct ContactSheetParams *iparams);
  bool create_document();
  bool add_newpage(const int32_t start_idx);
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
  int32_t rows_, cols_;

  /** Thumbnail size. */
  float2 thumb_size_;

  /** Gap size between images. */
  float2 gap_size_;
  /** Thumbnail offset. */
  float2 offset_;

  /** Get size of thumbnail relative to page canvas size. */
  void get_thumbnail_size(int iw, int ih);
  /** Add a text to the pdf. */
  void write_text(float2 loc, const char *text);
  /** Draw main page frame. */
  void draw_frame(void);
  /** Draw image to the canvas. */
  void draw_thumbnail(HPDF_Image pdf_image, int row, int col, int key);
};

}  // namespace blender::io::gpencil
