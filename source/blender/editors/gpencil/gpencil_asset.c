/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2022 Blender Foundation. */

/** \file
 * \ingroup edgpencil
 */

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "MEM_guardedalloc.h"

#include "DNA_gpencil_types.h"
#include "DNA_material_types.h"

#include "BKE_asset.h"
#include "BKE_context.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_geom.h"
#include "BKE_lib_id.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_object.h"
#include "BKE_report.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "GPU_immediate.h"

#include "ED_asset.h"
#include "ED_gpencil.h"
#include "ED_keyframing.h"
#include "ED_screen.h"
#include "ED_space_api.h"

#include "DEG_depsgraph.h"

#include "gpencil_intern.h"

#define ROTATION_CONTROL_GAP 15.0f

typedef struct tGPDAssetStroke {
  bGPDlayer *gpl;
  bGPDframe *gpf;
  bGPDstroke *gps;
  int slot_index;
  bool is_new_gpl;
  bool is_new_gpf;
} tGPDAssetStroke;

/* Temporary Asset import operation data. */
typedef struct tGPDasset {
  struct Main *bmain;
  struct Depsgraph *depsgraph;
  struct Scene *scene;
  struct ScrArea *area;
  struct ARegion *region;
  /** Current object. */
  struct Object *ob;
  /** Current GP data block. */
  struct bGPdata *gpd;
  /** Asset GP data block. */
  struct bGPdata *gpd_asset;
  /* Space Conversion Data */
  struct GP_SpaceConversion gsc;

  /** Current frame number. */
  int cframe;

  /** Drop initial position. */
  int drop[2];

  /* Data to keep a reference of the asset data inserted in the target object. */
  /** Number of elements in data. */
  int data_len;
  /** Array of data with all strokes append. */
  tGPDAssetStroke *data;

} tGPDasset;

/* -------------------------------------------------------------------- */
/** \name Create Grease Pencil data block Asset operator
 * \{ */

typedef enum eGP_AssetModes {
  /* Active Layer. */
  GP_ASSET_MODE_ACTIVE_LAYER = 0,
  /* All Layers. */
  GP_ASSET_MODE_ALL_LAYERS,
  /* All Layers in separated assets. */
  GP_ASSET_MODE_ALL_LAYERS_SPLIT,
  /* Active Frame. */
  GP_ASSET_MODE_ACTIVE_FRAME,
  /* Active Frame All Layers. */
  GP_ASSET_MODE_ACTIVE_FRAME_ALL_LAYERS,
  /* Selected Frames. */
  GP_ASSET_MODE_SELECTED_FRAMES,
  /* Selected Strokes. */
  GP_ASSET_MODE_SELECTED_STROKES,
} eGP_AssetModes;

/* Helper: Apply layer settings. */
static void apply_layer_settings(bGPDlayer *gpl)
{
  /* Apply layer attributes. */
  LISTBASE_FOREACH (bGPDframe *, gpf, &gpl->frames) {
    LISTBASE_FOREACH (bGPDstroke *, gps, &gpf->strokes) {
      gps->fill_opacity_fac *= gpl->opacity;
      gps->vert_color_fill[3] *= gpl->opacity;
      for (int p = 0; p < gps->totpoints; p++) {
        bGPDspoint *pt = &gps->points[p];
        float factor = (((float)gps->thickness * pt->pressure) + (float)gpl->line_change) /
                       ((float)gps->thickness * pt->pressure);
        pt->pressure *= factor;
        pt->strength *= gpl->opacity;

        /* Layer transformation. */
        mul_v3_m4v3(&pt->x, gpl->layer_mat, &pt->x);
        zero_v3(gpl->location);
        zero_v3(gpl->rotation);
        copy_v3_fl(gpl->scale, 1.0f);
      }
    }
  }

  gpl->line_change = 0;
  gpl->opacity = 1.0f;
  unit_m4(gpl->layer_mat);
  invert_m4_m4(gpl->layer_invmat, gpl->layer_mat);
}

/* Helper: Create an asset for data block.
 * return: False if there are features non supported. */
static bool gpencil_asset_create(const bContext *C,
                                 const bGPdata *gpd_src,
                                 const bGPDlayer *gpl_filter,
                                 const eGP_AssetModes mode,
                                 const bool reset_origin,
                                 const bool flatten_layers)
{
  Main *bmain = CTX_data_main(C);
  bool non_supported_feature = false;

  /* Create a copy of selected data block. */
  bGPdata *gpd = (bGPdata *)BKE_id_copy(bmain, &gpd_src->id);
  /* Enable fake user by default. */
  id_fake_user_set(&gpd->id);
  /* Disable Edit mode. */
  gpd->flag &= ~GP_DATA_STROKE_EDITMODE;

  const bGPDlayer *gpl_active = BKE_gpencil_layer_active_get(gpd);

  bool is_animation = false;

  LISTBASE_FOREACH_MUTABLE (bGPDlayer *, gpl, &gpd->layers) {
    /* If layer is hidden, remove. */
    if (gpl->flag & GP_LAYER_HIDE) {
      BKE_gpencil_layer_delete(gpd, gpl);
      continue;
    }

    /* If Active Layer or Active Frame mode, delete non active layers. */
    if ((ELEM(mode, GP_ASSET_MODE_ACTIVE_LAYER, GP_ASSET_MODE_ACTIVE_FRAME)) &&
        (gpl != gpl_active)) {
      BKE_gpencil_layer_delete(gpd, gpl);
      continue;
    }

    /* For splitting, remove if layer is not equals to filter parameter. */
    if (mode == GP_ASSET_MODE_ALL_LAYERS_SPLIT) {
      if (!STREQ(gpl_filter->info, gpl->info)) {
        BKE_gpencil_layer_delete(gpd, gpl);
        continue;
      }
    }

    /* Remove parenting data (feature non supported in data block). */
    if (gpl->parent != NULL) {
      gpl->parent = NULL;
      gpl->parsubstr[0] = 0;
      gpl->partype = 0;
      non_supported_feature = true;
    }

    /* Remove masking (feature non supported in data block). */
    if (gpl->mask_layers.first) {
      bGPDlayer_Mask *mask_next;
      for (bGPDlayer_Mask *mask = gpl->mask_layers.first; mask; mask = mask_next) {
        mask_next = mask->next;
        BKE_gpencil_layer_mask_remove(gpl, mask);
      }
      gpl->mask_layers.first = NULL;
      gpl->mask_layers.last = NULL;

      non_supported_feature = true;
    }

    const bGPDframe *gpf_active = gpl->actframe;

    LISTBASE_FOREACH_MUTABLE (bGPDframe *, gpf, &gpl->frames) {
      /* If Active Frame mode, delete non active frames. */
      if ((ELEM(mode, GP_ASSET_MODE_ACTIVE_FRAME, GP_ASSET_MODE_ACTIVE_FRAME_ALL_LAYERS)) &&
          (gpf != gpf_active)) {
        BKE_gpencil_layer_frame_delete(gpl, gpf);
        continue;
      }

      /* Remove if Selected frames mode and frame is not selected. */
      if ((mode == GP_ASSET_MODE_SELECTED_FRAMES) && ((gpf->flag & GP_FRAME_SELECT) == 0)) {
        BKE_gpencil_layer_frame_delete(gpl, gpf);
        continue;
      }

      /* Remove any unselected stroke if selected strokes mode. */
      if (mode == GP_ASSET_MODE_SELECTED_STROKES) {
        LISTBASE_FOREACH_MUTABLE (bGPDstroke *, gps, &gpf->strokes) {
          if ((gps->flag & GP_STROKE_SELECT) == 0) {
            BLI_remlink(&gpf->strokes, gps);
            BKE_gpencil_free_stroke(gps);
            continue;
          }
        }
      }
      /* Unselect all strokes and points. */
      gpd->select_last_index = 0;
      LISTBASE_FOREACH (bGPDstroke *, gps, &gpf->strokes) {
        gps->flag &= ~GP_STROKE_SELECT;
        BKE_gpencil_stroke_select_index_reset(gps);
        bGPDspoint *pt;
        int i;
        for (i = 0, pt = gps->points; i < gps->totpoints; i++, pt++) {
          pt->flag &= ~GP_SPOINT_SELECT;
        }
      }

      /* If Frame is empty, remove. */
      if (BLI_listbase_count(&gpf->strokes) == 0) {
        BKE_gpencil_layer_frame_delete(gpl, gpf);
      }
    }

    /* If there are more than one frame in the same layer, then is an animation. */
    is_animation |= (BLI_listbase_count(&gpl->frames) > 1);
  }

  /* Set origin to bounding box of strokes. */
  if (reset_origin) {
    float gpcenter[3];
    BKE_gpencil_centroid_3d(gpd, gpcenter);

    LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
      LISTBASE_FOREACH (bGPDframe *, gpf, &gpl->frames) {
        LISTBASE_FOREACH (bGPDstroke *, gps, &gpf->strokes) {
          bGPDspoint *pt;
          int i;
          for (i = 0, pt = gps->points; i < gps->totpoints; i++, pt++) {
            sub_v3_v3(&pt->x, gpcenter);
          }
          BKE_gpencil_stroke_boundingbox_calc(gps);
        }
      }
    }
  }

  /* Flatten layers. */
  if ((flatten_layers) && (gpd->layers.first)) {
    /* Apply layer attributes to all layers. */
    LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
      apply_layer_settings(gpl);
    }

    bGPDlayer *gpl_dst = gpd->layers.first;
    LISTBASE_FOREACH_BACKWARD_MUTABLE (bGPDlayer *, gpl, &gpd->layers) {
      if (gpl == gpl_dst) {
        break;
      }
      ED_gpencil_layer_merge(gpd, gpl, gpl->prev, false);
    }
    strcpy(gpl_dst->info, "Asset_Layer");
  }

  int f_min, f_max;
  BKE_gpencil_frame_min_max(gpd, &f_min, &f_max);

  /* Mark as asset. */
  if (ED_asset_mark_id(&gpd->id)) {
    ED_asset_generate_preview(C, &gpd->id);
    /* Retime frame number to start by 1. Must be done after generate the render preview. */
    LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
      LISTBASE_FOREACH (bGPDframe *, gpf, &gpl->frames) {
        gpf->framenum -= f_min - 1;
      }
    }
  }

  return non_supported_feature;
}

static bool gpencil_asset_edit_poll(bContext *C)
{
  const enum eContextObjectMode mode = CTX_data_mode_enum(C);

  /* Only allowed in Grease Pencil Edit mode. */
  if (mode != CTX_MODE_EDIT_GPENCIL) {
    return false;
  }

  Object *ob = CTX_data_active_object(C);
  if ((ob == NULL) || (ob->type != OB_GPENCIL)) {
    return false;
  }

  return ED_operator_view3d_active(C);
}

static int gpencil_asset_create_exec(const bContext *C, const wmOperator *op)
{
  Object *ob = CTX_data_active_object(C);
  bGPdata *gpd_src = ob->data;

  const eGP_AssetModes mode = RNA_enum_get(op->ptr, "mode");
  const bool reset_origin = RNA_boolean_get(op->ptr, "reset_origin");
  const bool flatten_layers = RNA_boolean_get(op->ptr, "flatten_layers");

  bool non_supported_feature = false;
  if (mode == GP_ASSET_MODE_ALL_LAYERS_SPLIT) {
    LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd_src->layers) {
      non_supported_feature |= gpencil_asset_create(
          C, gpd_src, gpl, mode, reset_origin, flatten_layers);
    }
  }
  else {
    non_supported_feature = gpencil_asset_create(
        C, gpd_src, NULL, mode, reset_origin, flatten_layers);
  }

  /* Warnings for non supported features in the created asset. */
  if ((non_supported_feature) || (ob->greasepencil_modifiers.first) || (ob->shader_fx.first)) {
    BKE_report(op->reports,
               RPT_WARNING,
               "Object has layer parenting, masking, modifiers or effects not supported in this "
               "asset type. These features have been omitted in the asset.");
  }

  WM_main_add_notifier(NC_ID | NA_EDITED, NULL);
  WM_main_add_notifier(NC_ASSET | NA_ADDED, NULL);

  return OPERATOR_FINISHED;
}

void GPENCIL_OT_asset_create(wmOperatorType *ot)
{
  static const EnumPropertyItem mode_types[] = {
      {GP_ASSET_MODE_ACTIVE_LAYER, "LAYER", 0, "Active Layer", ""},
      {GP_ASSET_MODE_ALL_LAYERS, "LAYERS_ALL", 0, "All Layers", ""},
      {GP_ASSET_MODE_ALL_LAYERS_SPLIT,
       "LAYERS_SPLIT",
       0,
       "All Layers Separated",
       "Create an asset by layer."},
      {GP_ASSET_MODE_ACTIVE_FRAME, "FRAME", 0, "Active Frame (Active Layer)", ""},
      {GP_ASSET_MODE_ACTIVE_FRAME_ALL_LAYERS, "FRAME_ALL", 0, "Active Frame (All Layers)", ""},
      {GP_ASSET_MODE_SELECTED_FRAMES, "FRAME_SELECTED", 0, "Selected Frames", ""},
      {GP_ASSET_MODE_SELECTED_STROKES, "SELECTED", 0, "Selected Strokes", ""},
      {0, NULL, 0, NULL, NULL},
  };

  /* identifiers */
  ot->name = "Create Grease Pencil Asset";
  ot->idname = "GPENCIL_OT_asset_create";
  ot->description = "Create asset from sections of the active object";

  /* callbacks */
  ot->invoke = WM_menu_invoke;
  ot->exec = gpencil_asset_create_exec;
  ot->poll = gpencil_asset_edit_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  ot->prop = RNA_def_enum(
      ot->srna, "mode", mode_types, GP_ASSET_MODE_SELECTED_STROKES, "Mode", "");
  RNA_def_boolean(ot->srna,
                  "reset_origin",
                  true,
                  "Origin to Geometry",
                  "Set origin of the asset in the center of the strokes bounding box");
  RNA_def_boolean(
      ot->srna, "flatten_layers", false, "Flatten Layers", "Merge all layers in only one");
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Import Grease Pencil Asset into existing data block operator
 * \{ */

/* Helper: Get a material from the data block array. */
static Material *gpencil_asset_material_get_from_id(ID *id, const int slot_index)
{
  const short *tot_slots_data_ptr = BKE_id_material_len_p(id);
  const int tot_slots_data = tot_slots_data_ptr ? *tot_slots_data_ptr : 0;
  if (slot_index >= tot_slots_data) {
    return NULL;
  }

  Material ***materials_data_ptr = BKE_id_material_array_p(id);
  Material **materials_data = materials_data_ptr ? *materials_data_ptr : NULL;
  Material *material = materials_data[slot_index];

  return material;
}

/* Helper: Set the selection of the imported strokes. */
static void gpencil_asset_set_selection(tGPDasset *tgpa, const bool enable)
{

  for (int index = 0; index < tgpa->data_len; index++) {
    tGPDAssetStroke *data = &tgpa->data[index];

    bGPDframe *gpf = data->gpf;
    if (enable) {
      gpf->flag |= GP_FRAME_SELECT;
    }
    else {
      gpf->flag &= ~GP_FRAME_SELECT;
    }

    bGPDstroke *gps = data->gps;
    if (enable) {
      gps->flag |= GP_STROKE_SELECT;
    }
    else {
      gps->flag &= ~GP_STROKE_SELECT;
    }

    bGPDspoint *pt;
    int i;

    for (i = 0, pt = gps->points; i < gps->totpoints; i++, pt++) {
      if (enable) {
        pt->flag |= GP_SPOINT_SELECT;
      }
      else {
        pt->flag &= ~GP_SPOINT_SELECT;
      }
    }

    /* Set selection index. */
    if (enable) {
      gps->flag |= GP_STROKE_SELECT;
      BKE_gpencil_stroke_select_index_set(tgpa->gpd, gps);
    }
    else {
      gps->flag &= ~GP_STROKE_SELECT;
      BKE_gpencil_stroke_select_index_reset(gps);
    }
  }
}

/* Helper: Append all strokes from the asset in the target data block. */
static bool gpencil_asset_append_strokes(tGPDasset *tgpa)
{
  bGPdata *gpd_target = tgpa->gpd;
  bGPdata *gpd_asset = tgpa->gpd_asset;

  /* Get the vector from origin to drop position. */
  float dest_pt[3];
  float loc2d[2];
  copy_v2fl_v2i(loc2d, tgpa->drop);
  gpencil_point_xy_to_3d(&tgpa->gsc, tgpa->scene, loc2d, dest_pt);

  float vec[3];
  sub_v3_v3v3(vec, dest_pt, tgpa->ob->loc);

  /* Count total of strokes. */
  tgpa->data_len = 0;
  LISTBASE_FOREACH (bGPDlayer *, gpl_asset, &gpd_asset->layers) {
    LISTBASE_FOREACH (bGPDframe *, gpf_asset, &gpl_asset->frames) {
      tgpa->data_len += BLI_listbase_count(&gpf_asset->strokes);
    }
  }

  /* If the asset is empty, exit. */
  if (tgpa->data_len == 0) {
    return false;
  }

  /* Alloc array of strokes. */
  tgpa->data = MEM_calloc_arrayN(tgpa->data_len, sizeof(tGPDAssetStroke), __func__);
  int data_index = 0;

  LISTBASE_FOREACH (bGPDlayer *, gpl_asset, &gpd_asset->layers) {
    /* Check if Layer is in target data block. */
    bGPDlayer *gpl_target = BKE_gpencil_layer_get_by_name(gpd_target, gpl_asset->info, false);

    bool is_new_gpl = false;
    if (gpl_target == NULL) {
      gpl_target = BKE_gpencil_layer_duplicate(gpl_asset, false, false);
      BLI_assert(gpl_target != NULL);
      gpl_target->actframe = NULL;
      BLI_listbase_clear(&gpl_target->frames);
      BLI_addtail(&gpd_target->layers, gpl_target);
      is_new_gpl = true;
    }

    LISTBASE_FOREACH (bGPDframe *, gpf_asset, &gpl_asset->frames) {
      /* Check if frame is in target layer. */
      int fra = tgpa->cframe + (gpf_asset->framenum - 1);
      bGPDframe *gpf_target = NULL;
      /* Find a frame in same frame number. */
      LISTBASE_FOREACH (bGPDframe *, gpf_find, &gpl_target->frames) {
        if (gpf_find->framenum == fra) {
          gpf_target = gpf_find;
          break;
        }
      }

      bool is_new_gpf = false;
      /* Check Rec button. If button is disabled, try to use active frame.
       * If no active keyframe, must create a new frame. */
      if ((gpf_target == NULL) && (!IS_AUTOKEY_ON(tgpa->scene))) {
        gpf_target = BKE_gpencil_layer_frame_get(gpl_target, fra, GP_GETFRAME_USE_PREV);
      }

      if (gpf_target == NULL) {
        gpf_target = BKE_gpencil_frame_addnew(gpl_target, fra);
        gpl_target->actframe = gpf_target;
        BLI_assert(gpf_target != NULL);
        BLI_listbase_clear(&gpf_target->strokes);
        is_new_gpf = true;
      }

      /* Loop all strokes and duplicate. */
      LISTBASE_FOREACH (bGPDstroke *, gps_asset, &gpf_asset->strokes) {
        if (gps_asset->mat_nr == -1) {
          continue;
        }

        bGPDstroke *gps_target = BKE_gpencil_stroke_duplicate(gps_asset, true, true);
        gps_target->next = gps_target->prev = NULL;
        gps_target->flag &= ~GP_STROKE_SELECT;
        BLI_addtail(&gpf_target->strokes, gps_target);

        /* Add the material. */
        Material *ma_src = gpencil_asset_material_get_from_id(&tgpa->gpd_asset->id,
                                                              gps_asset->mat_nr);

        int mat_index = (ma_src != NULL) ? BKE_gpencil_object_material_index_get_by_name(
                                               tgpa->ob, ma_src->id.name + 2) :
                                           -1;
        bool is_new_mat = false;
        if (mat_index == -1) {
          const int totcolors = tgpa->ob->totcol;
          mat_index = BKE_gpencil_object_material_ensure(tgpa->bmain, tgpa->ob, ma_src);
          if (tgpa->ob->totcol > totcolors) {
            is_new_mat = true;
          }
        }

        gps_target->mat_nr = mat_index;

        /* Apply the offset to drop position and unselect points. */
        bGPDspoint *pt;
        int i;
        for (i = 0, pt = gps_target->points; i < gps_target->totpoints; i++, pt++) {
          add_v3_v3(&pt->x, vec);
          pt->flag &= ~GP_SPOINT_SELECT;
        }

        /* Calc stroke bounding box. */
        BKE_gpencil_stroke_boundingbox_calc(gps_target);

        /* Add the reference to the stroke. */
        tGPDAssetStroke *data = &tgpa->data[data_index];
        data->gpl = gpl_target;
        data->gpf = gpf_target;
        data->gps = gps_target;
        data->is_new_gpl = is_new_gpl;
        data->is_new_gpf = is_new_gpf;
        data->slot_index = (is_new_mat) ? gps_target->mat_nr + 1 : -1;
        data_index++;

        /* Reset flags. */
        is_new_gpl = false;
        is_new_gpf = false;
      }
    }
  }

  /* Unselect any frame and stroke. */
  LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd_target->layers) {
    LISTBASE_FOREACH (bGPDframe *, gpf, &gpl->frames) {
      gpf->flag &= ~GP_FRAME_SELECT;
      LISTBASE_FOREACH (bGPDstroke *, gps, &gpf->strokes) {
        gps->flag &= ~GP_STROKE_SELECT;
        bGPDspoint *pt;
        int i;
        for (i = 0, pt = gps->points; i < gps->totpoints; i++, pt++) {
          pt->flag &= ~GP_SPOINT_SELECT;
        }
      }
    }
  }

  return true;
}

/* Exit and free memory */
static void gpencil_asset_import_exit(bContext *C, wmOperator *op)
{
  tGPDasset *tgpa = op->customdata;

  if (tgpa) {
    bGPdata *gpd = tgpa->gpd;

    /* Free data. */
    MEM_SAFE_FREE(tgpa->data);

    MEM_SAFE_FREE(tgpa);
    DEG_id_tag_update(&gpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
  }

  WM_event_add_notifier(C, NC_GPENCIL | NA_EDITED | ND_DATA, NULL);

  /* Clear pointer. */
  op->customdata = NULL;
}

/* Allocate memory and initialize values */
static tGPDasset *gpencil_session_init_asset_import(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  ID *id = NULL;

  PropertyRNA *prop_name = RNA_struct_find_property(op->ptr, "name");
  PropertyRNA *prop_type = RNA_struct_find_property(op->ptr, "type");

  /* These shouldn't fail when created by outliner dropping as it checks the ID is valid. */
  if (!RNA_property_is_set(op->ptr, prop_name) || !RNA_property_is_set(op->ptr, prop_type)) {
    return NULL;
  }
  const short id_type = RNA_property_enum_get(op->ptr, prop_type);
  char name[MAX_ID_NAME - 2];
  RNA_property_string_get(op->ptr, prop_name, name);
  id = BKE_libblock_find_name(bmain, id_type, name);
  if (id == NULL) {
    return NULL;
  }
  const int object_type = BKE_object_obdata_to_type(id);
  if (object_type != OB_GPENCIL) {
    return NULL;
  }

  tGPDasset *tgpa = MEM_callocN(sizeof(tGPDasset), "GPencil Asset Import Data");

  /* Save current settings. */
  tgpa->bmain = CTX_data_main(C);
  tgpa->depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  tgpa->scene = CTX_data_scene(C);
  tgpa->area = CTX_wm_area(C);
  tgpa->region = CTX_wm_region(C);
  tgpa->ob = CTX_data_active_object(C);

  /* Setup space conversions data. */
  gpencil_point_conversion_init(C, &tgpa->gsc);

  /* Save current frame number. */
  tgpa->cframe = tgpa->scene->r.cfra;

  /* Target GP data block. */
  tgpa->gpd = tgpa->ob->data;
  /* Asset GP data block. */
  tgpa->gpd_asset = (bGPdata *)id;

  tgpa->data_len = 0;
  tgpa->data = NULL;

  return tgpa;
}

/* Init: Allocate memory and set init values */
static bool gpencil_asset_import_init(bContext *C, wmOperator *op)
{
  tGPDasset *tgpa;

  /* check context */
  tgpa = op->customdata = gpencil_session_init_asset_import(C, op);
  if (tgpa == NULL) {
    /* something wasn't set correctly in context */
    gpencil_asset_import_exit(C, op);
    return false;
  }

  return true;
}

/* Invoke handler: Initialize the operator. */
static int gpencil_asset_import_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  bGPdata *gpd = CTX_data_gpencil_data(C);
  tGPDasset *tgpa = NULL;

  /* Try to initialize context data needed. */
  if (!gpencil_asset_import_init(C, op)) {
    if (op->customdata) {
      MEM_freeN(op->customdata);
    }
    return OPERATOR_CANCELLED;
  }
  tgpa = op->customdata;

  /* Save initial position of drop.  */
  tgpa->drop[0] = event->mval[0];
  tgpa->drop[1] = event->mval[1];

  /* Load of the strokes in the target data block. */
  if (!gpencil_asset_append_strokes(tgpa)) {
    gpencil_asset_import_exit(C, op);
    return OPERATOR_CANCELLED;
  }

  /* Select imported strokes. */
  gpencil_asset_set_selection(tgpa, true);
  /* Clean up temp data. */
  gpencil_asset_import_exit(C, op);

  DEG_id_tag_update(&gpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
  WM_event_add_notifier(C, NC_GPENCIL | NA_EDITED, NULL);

  return OPERATOR_FINISHED;
}

void GPENCIL_OT_asset_import(wmOperatorType *ot)
{

  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Grease Pencil Import Asset";
  ot->idname = "GPENCIL_OT_asset_import";
  ot->description = "Import Asset into existing grease pencil object";

  /* callbacks */
  ot->invoke = gpencil_asset_import_invoke;
  ot->poll = gpencil_asset_edit_poll;

  /* flags */
  ot->flag = OPTYPE_UNDO | OPTYPE_BLOCKING;

  /* Properties. */
  RNA_def_string(ot->srna, "name", "Name", MAX_ID_NAME - 2, "Name", "ID name to add");
  prop = RNA_def_enum(ot->srna, "type", rna_enum_id_type_items, 0, "Type", "");
  RNA_def_property_translation_context(prop, BLT_I18NCONTEXT_ID_ID);
}
/** \} */
