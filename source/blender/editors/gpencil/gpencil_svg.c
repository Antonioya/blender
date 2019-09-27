/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2011 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup edgpencil
 */

#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_gpencil_types.h"
#include "DNA_object_types.h"
#include "DNA_listBase.h"
#include "DNA_text_types.h"
#include "DNA_scene_types.h"

#include "BKE_context.h"
#include "BKE_gpencil.h"
#include "BKE_text.h"

#include "DEG_depsgraph.h"

#include "BKE_context.h"
#include "BKE_gpencil.h"
#include "BKE_report.h"

#include "UI_interface.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_gpencil.h"
#include "ED_select_utils.h"
#include "ED_svg.h"

static int gpencil_export_svg_exec(bContext *C, wmOperator *op)
{
  Object *gpobj = CTX_data_active_object(C);
  bGPdata *gpd = gpobj->data;
  bGPDlayer *gpl;

  for (gpl = gpd->layers.first; gpl; gpl = gpl->next) {
    Text *ta = BKE_text_add(CTX_data_main(C), "exported_svg");
    ED_svg_data_from_gpencil(gpd, ta, gpl, CTX_data_scene(C)->r.cfra);
  }

  return OPERATOR_FINISHED;
}

static bool gpencil_export_svg_poll(bContext *C)
{
  Object *o = CTX_data_active_object(C);

  if (o && o->type == OB_GPENCIL) {
    return true;
  }
  return false;
}

void GPENCIL_OT_export_svg(wmOperatorType *ot)
{
  PropertyRNA *prop;
  UNUSED_VARS(prop);

  /* identifiers */
  ot->name = "Export to SVG";
  ot->description = "Export this GPencil object to a SVG file";
  ot->idname = "GPENCIL_OT_export_svg";

  /* callbacks */
  ot->exec = gpencil_export_svg_exec;
  ot->poll = gpencil_export_svg_poll;

  /* properties */
  /* Should have: facing, layer, visibility, file split... */
}
