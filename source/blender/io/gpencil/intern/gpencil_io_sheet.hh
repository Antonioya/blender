/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2020 Blender Foundation. All rights reserved. */
#pragma once

/** \file
 * \ingroup bgpencil
 */

#include "DNA_space_types.h" /* for FILE_MAX */

#include "hpdf.h"

struct ContactSheetParams;

#define PDF_SHEET_EXPORTER_NAME "PDF Contact Sheet"
#define PDF_SHEET_EXPORTER_VERSION "v1.0"

namespace blender::io::gpencil {

class ContactSheetPDF {

 public:
  ContactSheetPDF(struct bContext *C, struct ContactSheetParams *iparams);
  bool new_document();
  bool add_newpage();
  bool add_body();
  bool write();

 protected:
  /* Data for easy access. */
  struct Main *bmain_;

 private:
  /** PDF document. */
  HPDF_Doc pdf_;
  /** PDF page. */
  HPDF_Page page_;
  /** Output PDF path. */
  char filepath_[FILE_MAX];

  /** Output PDF size. */
  int16_t render_x_, render_y_;

  /** Create PDF document. */
  bool create_document();
  /** Add page. */
  bool add_page();
};

}  // namespace blender::io::gpencil
