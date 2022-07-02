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

#ifdef WIN32
#  include "utfconv.h"
#endif

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
  ContactSheetItem *item = &iparams->items[0];
  filepath_[0] = '\0';

  char path_dir[FILE_MAXDIR];
  char path_file[FILE_MAXFILE];
  BLI_split_dirfile(item->path, path_dir, path_file, FILE_MAXDIR, FILE_MAXFILE);
  BLI_path_join(filepath_, sizeof(filepath_), path_dir, "mysheet.pdf", nullptr);

  bmain_ = CTX_data_main(C);

  pdf_ = nullptr;
  page_ = nullptr;

  render_x_ = 1980;
  render_y_ = 1080;
}

bool ContactSheetPDF::add_newpage()
{
  return add_page();
}

bool ContactSheetPDF::add_body()
{
  // TODO: Create a page
  return true;
}

bool ContactSheetPDF::write()
{
  HPDF_STATUS res = 0;
  res = HPDF_SaveToFile(pdf_, filepath_);
  return (res == 0) ? true : false;
}

bool ContactSheetPDF::new_document()
{
  pdf_ = HPDF_New(error_handler, nullptr);
  if (!pdf_) {
    printf("error: cannot create PdfDoc object\n");
    return false;
  }
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

  HPDF_Page_SetWidth(page_, render_x_);
  HPDF_Page_SetHeight(page_, render_y_);

  return true;
}

}  // namespace blender::io::gpencil
