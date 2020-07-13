/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
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
 * along with this program; if not, write to the Free Software  Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2017, Blender Foundation
 * This is a new part of Blender
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 */

/** \file blender/gpencil_modifiers/intern/MOD_gpencilstrokes.c
 *  \ingroup modifiers
 */

#include <stdio.h>

#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "DNA_gpencil_modifier_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"

#include "BKE_context.h"
#include "BKE_gpencil_geom.h"
#include "BKE_gpencil_modifier.h"
#include "BKE_lib_query.h"
#include "BKE_main.h"
#include "BKE_modifier.h"
#include "BKE_screen.h"

#include "MEM_guardedalloc.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "RNA_access.h"

#include "MOD_gpencil_modifiertypes.h"
#include "MOD_gpencil_ui_common.h"
#include "MOD_gpencil_util.h"

#include "DEG_depsgraph.h"

static void initData(GpencilModifierData *md)
{
  LengthGpencilModifierData *gpmd = (LengthGpencilModifierData *)md;
  gpmd->start_fac = 0.1f;
  gpmd->end_fac = 0.1f;
  gpmd->start_length = 0.1f;
  gpmd->end_length = 0.1f;
  gpmd->overshoot_fac = 0.01f;
  gpmd->pass_index = 0;
  gpmd->material = NULL;
}

static void copyData(const GpencilModifierData *md, GpencilModifierData *target)
{
  BKE_gpencil_modifier_copydata_generic(md, target);
}

static void applyLength(LengthGpencilModifierData *lmd, bGPDstroke *gps)
{
  bool changed = false;
  float len = BKE_gpencil_stroke_length(gps, true);
  if (len < FLT_EPSILON) {
    return;
  }

  if (lmd->mode == GP_LENGTH_ABSOLUTE) {
    if (lmd->start_length > 0.0f) {
      BKE_gpencil_stroke_stretch(gps, lmd->start_length, lmd->overshoot_fac, 1);
    }
    else if (lmd->start_length < 0.0f) {
      changed |= BKE_gpencil_stroke_shrink(gps, fabs(lmd->start_length), 1);
    }

    if (lmd->end_length > 0.0f) {
      BKE_gpencil_stroke_stretch(gps, lmd->end_length, lmd->overshoot_fac, 2);
    }
    else if (lmd->end_length < 0.0f) {
      changed |= BKE_gpencil_stroke_shrink(gps, fabs(lmd->end_length), 2);
    }
  }
  else {
    float relative = len * lmd->start_fac;
    if (relative > 0.0f) {
      BKE_gpencil_stroke_stretch(gps, relative, lmd->overshoot_fac, 1);
    }
    else if (lmd->start_fac < 0.0f) {
      changed |= BKE_gpencil_stroke_shrink(gps, fabs(relative), 1);
    }

    relative = len * lmd->end_fac;
    if (lmd->end_fac > 0.0f) {
      BKE_gpencil_stroke_stretch(gps, relative, lmd->overshoot_fac, 2);
    }
    else if (lmd->end_fac < 0.0f) {
      changed |= BKE_gpencil_stroke_shrink(gps, fabs(relative), 2);
    }
  }

  if (changed) {
    BKE_gpencil_stroke_geometry_update(gps);
  }
}

static void bakeModifier(Main *UNUSED(bmain),
                         Depsgraph *depsgraph,
                         GpencilModifierData *md,
                         Object *ob)
{

  bGPdata *gpd = ob->data;

  LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
    LISTBASE_FOREACH (bGPDframe *, gpf, &gpl->frames) {
      LengthGpencilModifierData *lmd = (LengthGpencilModifierData *)md;
      LISTBASE_FOREACH (bGPDstroke *, gps, &gpf->strokes) {
        if (is_stroke_affected_by_modifier(ob,
                                           lmd->layername,
                                           lmd->material,
                                           lmd->pass_index,
                                           lmd->layer_pass,
                                           1,
                                           gpl,
                                           gps,
                                           lmd->flag & GP_LENGTH_INVERT_LAYER,
                                           lmd->flag & GP_LENGTH_INVERT_PASS,
                                           lmd->flag & GP_LENGTH_INVERT_LAYERPASS,
                                           lmd->flag & GP_LENGTH_INVERT_MATERIAL)) {
          applyLength(lmd, gps);
        }
      }
    }
  }
}

/* -------------------------------- */

/* Generic "generateStrokes" callback */
static void deformStroke(GpencilModifierData *md,
                         Depsgraph *depsgraph,
                         Object *ob,
                         bGPDlayer *gpl,
                         bGPDframe *UNUSED(gpf),
                         bGPDstroke *gps)
{
  LengthGpencilModifierData *lmd = (LengthGpencilModifierData *)md;
  if (is_stroke_affected_by_modifier(ob,
                                     lmd->layername,
                                     lmd->material,
                                     lmd->pass_index,
                                     lmd->layer_pass,
                                     1,
                                     gpl,
                                     gps,
                                     lmd->flag & GP_LENGTH_INVERT_LAYER,
                                     lmd->flag & GP_LENGTH_INVERT_PASS,
                                     lmd->flag & GP_LENGTH_INVERT_LAYERPASS,
                                     lmd->flag & GP_LENGTH_INVERT_MATERIAL)) {
    applyLength(lmd, gps);
  }
}

static void panel_draw(const bContext *C, Panel *panel)
{
  uiLayout *layout = panel->layout;

  PointerRNA ptr;
  gpencil_modifier_panel_get_property_pointers(C, panel, NULL, &ptr);
  const int mode = RNA_enum_get(&ptr, "mode");

  uiLayoutSetPropSep(layout, true);
  uiItemR(layout, &ptr, "mode", 0, NULL, ICON_NONE);

  if (mode == GP_LENGTH_RELATIVE) {
    uiItemR(layout, &ptr, "start_factor", 0, NULL, ICON_NONE);
    uiItemR(layout, &ptr, "end_factor", 0, NULL, ICON_NONE);
  }
  else {
    uiItemR(layout, &ptr, "start_length", 0, NULL, ICON_NONE);
    uiItemR(layout, &ptr, "end_length", 0, NULL, ICON_NONE);
  }
  uiItemR(layout, &ptr, "overshoot_factor", UI_ITEM_R_SLIDER, NULL, ICON_NONE);

  gpencil_modifier_panel_end(layout, &ptr);
}

static void foreachIDLink(GpencilModifierData *md, Object *ob, IDWalkFunc walk, void *userData)
{
  LengthGpencilModifierData *mmd = (LengthGpencilModifierData *)md;

  walk(userData, ob, (ID **)&mmd->material, IDWALK_CB_USER);
}

static void mask_panel_draw(const bContext *C, Panel *panel)
{
  gpencil_modifier_masking_panel_draw(C, panel, true, false);
}

static void panelRegister(ARegionType *region_type)
{
  PanelType *panel_type = gpencil_modifier_panel_register(
      region_type, eGpencilModifierType_Length, panel_draw);
  gpencil_modifier_subpanel_register(
      region_type, "mask", "Influence", NULL, mask_panel_draw, panel_type);
}

GpencilModifierTypeInfo modifierType_Gpencil_Length = {
    /* name */ "Length",
    /* structName */ "LengthGpencilModifierData",
    /* structSize */ sizeof(LengthGpencilModifierData),
    /* type */ eGpencilModifierTypeType_Gpencil,
    /* flags */ eGpencilModifierTypeFlag_SupportsEditmode,

    /* copyData */ copyData,

    /* deformStroke */ deformStroke,
    /* generateStrokes */ NULL,
    /* bakeModifier */ bakeModifier,
    /* remapTime */ NULL,

    /* initData */ initData,
    /* freeData */ NULL,
    /* isDisabled */ NULL,
    /* updateDepsgraph */ NULL,
    /* dependsOnTime */ NULL,
    /* foreachObjectLink */ NULL,
    /* foreachIDLink */ foreachIDLink,
    /* foreachTexLink */ NULL,
    /* panelRegister */ panelRegister,
};
