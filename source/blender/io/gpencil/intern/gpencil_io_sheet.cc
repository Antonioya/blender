/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2022 Blender Foundation. All rights reserved. */

/** \file
 * \ingroup bgpencil
 */
#include "BLI_path_util.h"
#include "BLI_string.h"

#include "DNA_screen_types.h"

#include "BKE_context.h"
#include "BKE_main.h"

#include "ED_view3d.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "gpencil_io.h"
#include "gpencil_io_sheet.hh"

static void error_handler(HPDF_STATUS error_no, HPDF_STATUS detail_no, void *UNUSED(user_data))
{
  printf("ERROR: error_no=%04X, detail_no=%u\n", (HPDF_UINT)error_no, (HPDF_UINT)detail_no);
}

namespace blender::io::gpencil {

/* Constructor. */
ContactSheetPDF::ContactSheetPDF(bContext *C, ContactSheetParams *iparams)
{
  params_ = *iparams;

  BLI_strncpy(filepath_, iparams->outpath, sizeof(filepath_));
  bmain_ = CTX_data_main(C);

  pdf_ = nullptr;
  page_ = nullptr;

  canvas_.x = iparams->canvas[0];
  canvas_.y = iparams->canvas[1];

  rows_ = iparams->rows;
  cols_ = iparams->cols;
}

bool ContactSheetPDF::add_newpage()
{
  return add_page();
}

bool ContactSheetPDF::write()
{
  HPDF_STATUS res = 0;
  res = HPDF_SaveToFile(pdf_, filepath_);
  return (res == 0) ? true : false;
}

void ContactSheetPDF::free_document()
{
  if (pdf_) {
    HPDF_Free(pdf_);
  }
}

bool ContactSheetPDF::new_document()
{
  pdf_ = HPDF_New(error_handler, nullptr);
  if (!pdf_) {
    printf("error: cannot create PdfDoc object\n");
    return false;
  }

  font_ = HPDF_GetFont(pdf_, "Helvetica", NULL);

  return true;
}

bool ContactSheetPDF::add_page()
{
  /* Add a new page object. */
  page_ = HPDF_AddPage(pdf_);
  if (!pdf_) {
    printf("error: cannot create PdfPage\n");
    return false;
  }

  HPDF_Page_SetWidth(page_, canvas_.x);
  HPDF_Page_SetHeight(page_, canvas_.y);

  ContactSheetItem *item = &params_.items[0];
  ImBuf *ibuf = IMB_loadiffname(item->path, 0, NULL);

  return true;
}

void ContactSheetPDF::get_thumbnail_size(HPDF_Image image)
{
  HPDF_UINT iw = HPDF_Image_GetWidth(image);
  HPDF_UINT ih = HPDF_Image_GetHeight(image);
  float x_size = canvas_.x / (float)(cols_ + 1);
  float y_size = canvas_.y / (float)(rows_ + 1);

  thumb_size_.x = (x_size <= y_size) ? x_size : y_size;
  thumb_size_.y = thumb_size_.x * ((float)ih / (float)iw);

  gap_size_[0] = (canvas_.x - (thumb_size_.x * (float)cols_)) / (float)cols_;
  gap_size_[1] = (canvas_.y - (thumb_size_.y * (float)rows_)) / (float)rows_;
}

}  // namespace blender::io::gpencil
