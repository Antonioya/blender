/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2022 Blender Foundation. All rights reserved. */

/** \file
 * \ingroup bgpencil
 */
#include "BLI_fileops.h"
#include "BLI_path_util.h"
#include "BLI_string.h"

#include "DNA_screen_types.h"

#include "BKE_appdir.h"
#include "BKE_context.h"
#include "BKE_image.h"
#include "BKE_image_save.h"
#include "BKE_lib_id.h"
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

const HPDF_REAL PAGE_MARGIN_X = 80;
const HPDF_REAL PAGE_MARGIN_Y = 80;

/* Constructor. */
ContactSheetPDF::ContactSheetPDF(bContext *C, ContactSheetParams *iparams)
{
  params_ = *iparams;

  BLI_strncpy(filepath_, iparams->outpath, sizeof(filepath_));
  bmain_ = CTX_data_main(C);
  scene_ = CTX_data_scene(C);

  pdf_ = nullptr;
  page_ = nullptr;

  page_size_.x = iparams->page_size[0];
  page_size_.y = iparams->page_size[1];

  canvas_size_.x = page_size_.x - (PAGE_MARGIN_X * 2);
  canvas_size_.y = page_size_.y - (PAGE_MARGIN_Y * 2);

  rows_ = iparams->rows;
  cols_ = iparams->cols;

  /* Total pages. */
  bypage_ = (rows_ * cols_);
  totpages_ = params_.len / bypage_;
  if ((totpages_ * bypage_) < params_.len) {
    totpages_++;
  }
}

bool ContactSheetPDF::save_document()
{
  HPDF_STATUS res = 0;
  res = HPDF_SaveToFile(pdf_, filepath_);
  if (res == 0) {
    std::cout << "File:" << filepath_ << " created.\n";
  }
  return (res == 0) ? true : false;
}

void ContactSheetPDF::free_document()
{
  if (pdf_) {
    HPDF_Free(pdf_);
  }
}

bool ContactSheetPDF::create_document()
{
  pdf_ = HPDF_New(error_handler, nullptr);
  if (!pdf_) {
    printf("error: cannot create PdfDoc object\n");
    return false;
  }

  font_ = HPDF_GetFont(pdf_, "Helvetica", NULL);

  return true;
}

void ContactSheetPDF::load_and_draw_image(ContactSheetItem *item, const int row, const int col)
{
  ImageUser *iuser = nullptr;
  /* Load original image from disk. */
  ImBuf *ibuf = IMB_loadiffname(item->path, 0, NULL);
  if (ibuf == nullptr) {
    return;
  }

  /* Scale image to thumbnail size. */
  IMB_scaleImBuf(ibuf, thumb_size_.x, thumb_size_.y);

  /* Save as Jpeg thumbnail to make it compatible with Libharu. */
  Image *ima = BKE_image_add_from_imbuf(bmain_, ibuf, "Thumb");
  IMB_freeImBuf(ibuf);
  if (ima == nullptr) {
    return;
  }

  ImageSaveOptions opts;
  if (BKE_image_save_options_init(&opts, bmain_, scene_, ima, nullptr, false, false)) {
    /* Save image in temp folder in jpeg format. */
    opts.im_format.imtype = R_IMF_IMTYPE_JPEG90;
    opts.im_format.compress = ibuf->foptions.quality;
    opts.im_format.planes = ibuf->planes;
    opts.im_format.quality = scene_->r.im_format.quality;
    BLI_join_dirfile(opts.filepath, sizeof(opts.filepath), BKE_tempdir_session(), "thumb.jpg");
    if (BKE_image_save(nullptr, bmain_, ima, iuser, &opts)) {
      /* Load the temp image thumbnail in libharu.*/
      HPDF_Image pdf_image = HPDF_LoadJpegImageFromFile(pdf_, opts.filepath);
      /* Draw thumbnail in pdf file. */
      draw_thumbnail(pdf_image, row, col, item);
    }
    /* Free memory. */
    BKE_image_save_options_free(&opts);
    /* Delete thumb image from memory. */
    BKE_id_free(bmain_, ima);
    /* Delete thumb image file created on temp folder. */
    if (BLI_exists(opts.filepath)) {
      BLI_delete(opts.filepath, false, false);
    }
  }
}

bool ContactSheetPDF::add_newpage(const uint32_t pagenum)
{
  ContactSheetItem *item = nullptr;

  /* Add a new page object. */
  page_ = HPDF_AddPage(pdf_);
  if (!pdf_) {
    printf("error: cannot create PdfPage\n");
    return false;
  }

  HPDF_Page_SetWidth(page_, page_size_.x);
  HPDF_Page_SetHeight(page_, page_size_.y);
  uint32_t start_idx = pagenum * bypage_;
  /* Calculate thumbnail size base on the size of first image. */
  if (pagenum == 0) {
    item = &params_.items[0];
    ImBuf *ibuf = IMB_loadiffname(item->path, 0, NULL);
    if (ibuf == nullptr) {
      return false;
    }
    get_thumbnail_size(ibuf->x, ibuf->y);
    IMB_freeImBuf(ibuf);
  }

  /* Draw page main frame. */
  draw_page_frame(pagenum);

  int idx = start_idx;
  bool doit = true;
  for (int r = rows_ - 1; r >= 0 && doit; r--) {
    for (int c = 0; c < cols_ && doit; c++) {
      item = &params_.items[idx];
      load_and_draw_image(item, r, c);
      idx++;
      if (idx == params_.len) {
        doit = false;
        break;
      }
    }
  }

  return true;
}

void ContactSheetPDF::get_thumbnail_size(int iw, int ih)
{
  const float ratio = (float)iw / (float)ih;
  const float oversize = ratio > 2.0f ? 0.0f : 0.25f;
  const float cols = (float)cols_;
  const float rows = (float)rows_;

  float x_size = canvas_size_.x / (cols + oversize);
  float y_size = canvas_size_.y / (rows + oversize);

  thumb_size_.x = (x_size <= y_size) ? x_size : y_size;
  thumb_size_.y = thumb_size_.x * ((float)ih / (float)iw);

  gap_size_.x = (canvas_size_.x - (thumb_size_.x * cols)) / cols;
  gap_size_.y = (canvas_size_.y - (thumb_size_.y * rows)) / rows;

  offset_.x = PAGE_MARGIN_X + (gap_size_.x * 0.5f);
  offset_.y = PAGE_MARGIN_Y + (gap_size_.y * 0.5f);
}

void ContactSheetPDF::write_text(float2 loc, const char *text)
{
  HPDF_Page_SetRGBFill(page_, 0, 0, 0);

  HPDF_Page_BeginText(page_);
  HPDF_Page_MoveTextPos(page_, loc.x, loc.y);
  HPDF_Page_ShowText(page_, text);
  HPDF_Page_EndText(page_);
}

void ContactSheetPDF::draw_page_frame(uint32_t pagenum)
{
  HPDF_Page_SetFontAndSize(page_, font_, 30);
  write_text(float2(PAGE_MARGIN_X, PAGE_MARGIN_Y - 30), params_.title);

  char buf[255];
  snprintf(buf, 255, "%4d/%4d", pagenum + 1, totpages_);
  write_text(float2(canvas_size_.x - 15, PAGE_MARGIN_Y - 30), buf);
}

void ContactSheetPDF::draw_thumbnail(HPDF_Image pdf_image,
                                     int row,
                                     int col,
                                     ContactSheetItem *item)
{
  if (pdf_image == nullptr) {
    return;
  }

  HPDF_REAL pos_x = offset_.x + ((thumb_size_.x + gap_size_.x) * col);
  HPDF_REAL pos_y = offset_.y + ((thumb_size_.y + gap_size_.y) * row);
  HPDF_Page_DrawImage(page_, pdf_image, pos_x, pos_y, thumb_size_.x, thumb_size_.y);
  HPDF_Page_SetLineWidth(page_, 0.5);
  HPDF_Page_Rectangle(page_, pos_x, pos_y, thumb_size_.x, thumb_size_.y);
  HPDF_Page_Stroke(page_);
  /* Text. */
  HPDF_Page_SetFontAndSize(page_, font_, 20);
  write_text(float2(pos_x, pos_y - 20), item->name);
}

}  // namespace blender::io::gpencil
