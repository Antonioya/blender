/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2020 Blender Foundation. All rights reserved. */

/** \file
 * \ingroup editor/io
 */

#ifdef WITH_IO_GPENCIL
#  include <string.h>

#  include "BLI_path_util.h"
#  include "BLI_string.h"

#  include "DNA_gpencil_types.h"
#  include "DNA_space_types.h"

#  include "MEM_guardedalloc.h"

#  include "BKE_gpencil.h"
#  include "BKE_main.h"
#  include "BKE_report.h"
#  include "BKE_screen.h"

#  include "BLT_translation.h"

#  include "ED_fileselect.h"

#  include "RNA_access.h"
#  include "RNA_define.h"

#  include "UI_interface.h"
#  include "UI_resources.h"

#  include "WM_api.h"
#  include "WM_types.h"

#  include "DEG_depsgraph.h"
#  include "DEG_depsgraph_query.h"

#  include "io_gpencil.h"

#  include "gpencil_io.h"

#  if defined(WITH_PUGIXML) || defined(WITH_HARU)
/* Definition of enum elements to export. */
/* Common props for exporting. */
static void gpencil_export_common_props_definition(wmOperatorType *ot)
{
  static const EnumPropertyItem select_items[] = {
      {GP_EXPORT_ACTIVE, "ACTIVE", 0, "Active", "Include only the active object"},
      {GP_EXPORT_SELECTED, "SELECTED", 0, "Selected", "Include selected objects"},
      {GP_EXPORT_VISIBLE, "VISIBLE", 0, "Visible", "Include all visible objects"},
      {0, NULL, 0, NULL, NULL},
  };

  RNA_def_boolean(ot->srna, "use_fill", true, "Fill", "Export strokes with fill enabled");
  RNA_def_enum(ot->srna,
               "selected_object_type",
               select_items,
               GP_EXPORT_SELECTED,
               "Object",
               "Which objects to include in the export");
  RNA_def_float(ot->srna,
                "stroke_sample",
                0.0f,
                0.0f,
                100.0f,
                "Sampling",
                "Precision of stroke sampling. Low values mean a more precise result, and zero "
                "disables sampling",
                0.0f,
                100.0f);
  RNA_def_boolean(ot->srna,
                  "use_normalized_thickness",
                  false,
                  "Normalize",
                  "Export strokes with constant thickness");
}
#  endif

/* <-------- SVG single frame export. --------> */
#  ifdef WITH_PUGIXML
static bool wm_gpencil_export_svg_common_check(bContext *UNUSED(C), wmOperator *op)
{
  char filepath[FILE_MAX];
  RNA_string_get(op->ptr, "filepath", filepath);

  if (!BLI_path_extension_check(filepath, ".svg")) {
    BLI_path_extension_ensure(filepath, FILE_MAX, ".svg");
    RNA_string_set(op->ptr, "filepath", filepath);
    return true;
  }

  return false;
}

static int wm_gpencil_export_svg_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
  ED_fileselect_ensure_default_filepath(C, op, ".svg");

  WM_event_add_fileselect(C, op);

  return OPERATOR_RUNNING_MODAL;
}

static int wm_gpencil_export_svg_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  Object *ob = CTX_data_active_object(C);

  if (!RNA_struct_property_is_set_ex(op->ptr, "filepath", false)) {
    BKE_report(op->reports, RPT_ERROR, "No filename given");
    return OPERATOR_CANCELLED;
  }

  ARegion *region = get_invoke_region(C);
  if (region == NULL) {
    BKE_report(op->reports, RPT_ERROR, "Unable to find valid 3D View area");
    return OPERATOR_CANCELLED;
  }
  View3D *v3d = get_invoke_view3d(C);

  char filename[FILE_MAX];
  RNA_string_get(op->ptr, "filepath", filename);

  const bool use_fill = RNA_boolean_get(op->ptr, "use_fill");
  const bool use_norm_thickness = RNA_boolean_get(op->ptr, "use_normalized_thickness");
  const eGpencilExportSelect select_mode = RNA_enum_get(op->ptr, "selected_object_type");

  const bool use_clip_camera = RNA_boolean_get(op->ptr, "use_clip_camera");

  /* Set flags. */
  int flag = 0;
  SET_FLAG_FROM_TEST(flag, use_fill, GP_EXPORT_FILL);
  SET_FLAG_FROM_TEST(flag, use_norm_thickness, GP_EXPORT_NORM_THICKNESS);
  SET_FLAG_FROM_TEST(flag, use_clip_camera, GP_EXPORT_CLIP_CAMERA);

  GpencilIOParams params = {.C = C,
                            .region = region,
                            .v3d = v3d,
                            .ob = ob,
                            .mode = GP_EXPORT_TO_SVG,
                            .frame_start = scene->r.cfra,
                            .frame_end = scene->r.cfra,
                            .frame_cur = scene->r.cfra,
                            .flag = flag,
                            .scale = 1.0f,
                            .select_mode = select_mode,
                            .frame_mode = GP_EXPORT_FRAME_ACTIVE,
                            .stroke_sample = RNA_float_get(op->ptr, "stroke_sample"),
                            .resolution = 1.0f};

  /* Do export. */
  WM_cursor_wait(true);
  const bool done = gpencil_io_export(filename, &params);
  WM_cursor_wait(false);

  if (!done) {
    BKE_report(op->reports, RPT_WARNING, "Unable to export SVG");
  }

  return OPERATOR_FINISHED;
}

static void ui_gpencil_export_svg_settings(uiLayout *layout, PointerRNA *imfptr)
{
  uiLayout *box, *row;

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  box = uiLayoutBox(layout);

  row = uiLayoutRow(box, false);
  uiItemL(row, IFACE_("Scene Options"), ICON_NONE);

  row = uiLayoutRow(box, false);
  uiItemR(row, imfptr, "selected_object_type", 0, NULL, ICON_NONE);

  box = uiLayoutBox(layout);
  row = uiLayoutRow(box, false);
  uiItemL(row, IFACE_("Export Options"), ICON_NONE);

  uiLayout *col = uiLayoutColumn(box, false);
  uiItemR(col, imfptr, "stroke_sample", 0, NULL, ICON_NONE);
  uiItemR(col, imfptr, "use_fill", 0, NULL, ICON_NONE);
  uiItemR(col, imfptr, "use_normalized_thickness", 0, NULL, ICON_NONE);
  uiItemR(col, imfptr, "use_clip_camera", 0, NULL, ICON_NONE);
}

static void wm_gpencil_export_svg_draw(bContext *UNUSED(C), wmOperator *op)
{
  ui_gpencil_export_svg_settings(op->layout, op->ptr);
}

static bool wm_gpencil_export_svg_poll(bContext *C)
{
  if ((CTX_wm_window(C) == NULL) || (CTX_data_mode_enum(C) != CTX_MODE_OBJECT)) {
    return false;
  }

  return true;
}

void WM_OT_gpencil_export_svg(wmOperatorType *ot)
{
  ot->name = "Export to SVG";
  ot->description = "Export grease pencil to SVG";
  ot->idname = "WM_OT_gpencil_export_svg";

  ot->invoke = wm_gpencil_export_svg_invoke;
  ot->exec = wm_gpencil_export_svg_exec;
  ot->poll = wm_gpencil_export_svg_poll;
  ot->ui = wm_gpencil_export_svg_draw;
  ot->check = wm_gpencil_export_svg_common_check;

  WM_operator_properties_filesel(ot,
                                 FILE_TYPE_FOLDER | FILE_TYPE_OBJECT_IO,
                                 FILE_BLENDER,
                                 FILE_SAVE,
                                 WM_FILESEL_FILEPATH | WM_FILESEL_SHOW_PROPS,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_DEFAULT);

  gpencil_export_common_props_definition(ot);

  RNA_def_boolean(ot->srna,
                  "use_clip_camera",
                  false,
                  "Clip Camera",
                  "Clip drawings to camera size when export in camera view");
}
#  endif

/* <-------- PDF single frame export. --------> */
#  ifdef WITH_HARU
static bool wm_gpencil_export_pdf_common_check(bContext *UNUSED(C), wmOperator *op)
{

  char filepath[FILE_MAX];
  RNA_string_get(op->ptr, "filepath", filepath);

  if (!BLI_path_extension_check(filepath, ".pdf")) {
    BLI_path_extension_ensure(filepath, FILE_MAX, ".pdf");
    RNA_string_set(op->ptr, "filepath", filepath);
    return true;
  }

  return false;
}

static int wm_gpencil_export_pdf_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
  ED_fileselect_ensure_default_filepath(C, op, ".pdf");

  WM_event_add_fileselect(C, op);

  return OPERATOR_RUNNING_MODAL;
}

static int wm_gpencil_export_pdf_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  Object *ob = CTX_data_active_object(C);

  if (!RNA_struct_property_is_set_ex(op->ptr, "filepath", false)) {
    BKE_report(op->reports, RPT_ERROR, "No filename given");
    return OPERATOR_CANCELLED;
  }

  ARegion *region = get_invoke_region(C);
  if (region == NULL) {
    BKE_report(op->reports, RPT_ERROR, "Unable to find valid 3D View area");
    return OPERATOR_CANCELLED;
  }
  View3D *v3d = get_invoke_view3d(C);

  char filename[FILE_MAX];
  RNA_string_get(op->ptr, "filepath", filename);

  const bool use_fill = RNA_boolean_get(op->ptr, "use_fill");
  const bool use_norm_thickness = RNA_boolean_get(op->ptr, "use_normalized_thickness");
  const short select_mode = RNA_enum_get(op->ptr, "selected_object_type");
  const short frame_mode = RNA_enum_get(op->ptr, "frame_mode");

  /* Set flags. */
  int flag = 0;
  SET_FLAG_FROM_TEST(flag, use_fill, GP_EXPORT_FILL);
  SET_FLAG_FROM_TEST(flag, use_norm_thickness, GP_EXPORT_NORM_THICKNESS);

  GpencilIOParams params = {.C = C,
                            .region = region,
                            .v3d = v3d,
                            .ob = ob,
                            .mode = GP_EXPORT_TO_PDF,
                            .frame_start = scene->r.sfra,
                            .frame_end = scene->r.efra,
                            .frame_cur = scene->r.cfra,
                            .flag = flag,
                            .scale = 1.0f,
                            .select_mode = select_mode,
                            .frame_mode = frame_mode,
                            .stroke_sample = RNA_float_get(op->ptr, "stroke_sample"),
                            .resolution = 1.0f};

  /* Do export. */
  WM_cursor_wait(true);
  const bool done = gpencil_io_export(filename, &params);
  WM_cursor_wait(false);

  if (!done) {
    BKE_report(op->reports, RPT_WARNING, "Unable to export PDF");
  }

  return OPERATOR_FINISHED;
}

static void ui_gpencil_export_pdf_settings(uiLayout *layout, PointerRNA *imfptr)
{
  uiLayout *box, *row, *col, *sub;

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  box = uiLayoutBox(layout);

  row = uiLayoutRow(box, false);
  uiItemL(row, IFACE_("Scene Options"), ICON_NONE);

  row = uiLayoutRow(box, false);
  uiItemR(row, imfptr, "selected_object_type", 0, NULL, ICON_NONE);

  box = uiLayoutBox(layout);
  row = uiLayoutRow(box, false);
  uiItemL(row, IFACE_("Export Options"), ICON_NONE);

  col = uiLayoutColumn(box, false);
  sub = uiLayoutColumn(col, true);
  uiItemR(sub, imfptr, "frame_mode", 0, IFACE_("Frame"), ICON_NONE);

  uiLayoutSetPropSep(box, true);

  sub = uiLayoutColumn(col, true);
  uiItemR(sub, imfptr, "stroke_sample", 0, NULL, ICON_NONE);
  uiItemR(sub, imfptr, "use_fill", 0, NULL, ICON_NONE);
  uiItemR(sub, imfptr, "use_normalized_thickness", 0, NULL, ICON_NONE);
}

static void wm_gpencil_export_pdf_draw(bContext *UNUSED(C), wmOperator *op)
{
  ui_gpencil_export_pdf_settings(op->layout, op->ptr);
}

static bool wm_gpencil_export_pdf_poll(bContext *C)
{
  if ((CTX_wm_window(C) == NULL) || (CTX_data_mode_enum(C) != CTX_MODE_OBJECT)) {
    return false;
  }

  return true;
}

void WM_OT_gpencil_export_pdf(wmOperatorType *ot)
{
  ot->name = "Export to PDF";
  ot->description = "Export grease pencil to PDF";
  ot->idname = "WM_OT_gpencil_export_pdf";

  ot->invoke = wm_gpencil_export_pdf_invoke;
  ot->exec = wm_gpencil_export_pdf_exec;
  ot->poll = wm_gpencil_export_pdf_poll;
  ot->ui = wm_gpencil_export_pdf_draw;
  ot->check = wm_gpencil_export_pdf_common_check;

  WM_operator_properties_filesel(ot,
                                 FILE_TYPE_FOLDER | FILE_TYPE_OBJECT_IO,
                                 FILE_BLENDER,
                                 FILE_SAVE,
                                 WM_FILESEL_FILEPATH | WM_FILESEL_SHOW_PROPS,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_DEFAULT);

  static const EnumPropertyItem gpencil_export_frame_items[] = {
      {GP_EXPORT_FRAME_ACTIVE, "ACTIVE", 0, "Active", "Include only active frame"},
      {GP_EXPORT_FRAME_SELECTED, "SELECTED", 0, "Selected", "Include selected frames"},
      {GP_EXPORT_FRAME_SCENE, "SCENE", 0, "Scene", "Include all scene frames"},
      {0, NULL, 0, NULL, NULL},
  };

  gpencil_export_common_props_definition(ot);
  ot->prop = RNA_def_enum(ot->srna,
                          "frame_mode",
                          gpencil_export_frame_items,
                          GP_EXPORT_ACTIVE,
                          "Frames",
                          "Which frames to include in the export");
}

static void contact_sheet_pdf_load_files(bContext *C,
                                         wmOperator *op,
                                         ContactSheetParams *load_data)
{
  PropertyRNA *prop;
  int idx = 0;
  load_data->outpath[0] = '\0';

  ED_fileselect_ensure_default_filepath(C, op, ".pdf");
  if ((prop = RNA_struct_find_property(op->ptr, "filepath"))) {
    RNA_property_string_get(op->ptr, prop, load_data->outpath);
    if (!BLI_path_extension_check(load_data->outpath, ".pdf")) {
      BLI_path_extension_ensure(load_data->outpath, FILE_MAX, ".pdf");
      RNA_string_set(op->ptr, "filepath", load_data->outpath);
    }
  }

  if ((prop = RNA_struct_find_property(op->ptr, "directory"))) {
    char *directory = RNA_string_get_alloc(op->ptr, "directory", NULL, 0, NULL);

    if ((prop = RNA_struct_find_property(op->ptr, "files"))) {
      RNA_PROP_BEGIN (op->ptr, itemptr, prop) {
        char *filename = RNA_string_get_alloc(&itemptr, "name", NULL, 0, NULL);

        /* Split the tokens. */
        const char sep[2] = "|";
        char *next_tokens = NULL;
        char *token = strtok_s(filename, sep, &next_tokens);

        /* Image filepath. */
        BLI_path_join(
            load_data->items[idx].path, sizeof(load_data->items[idx].path), directory, token);

        load_data->items[idx].name[0] = '\0';
        load_data->items[idx].data[0] = '\0';

        /* Check if more tokens. If not, use first token to determine name. */
        if (next_tokens[0] == '\0') {
          BLI_split_file_part(
              filename, load_data->items[idx].name, sizeof(load_data->items[idx].name) - 1);
          /* Remove extension from the file name. */
          const char *ext = BLI_path_extension(load_data->items[idx].name);
          if (ext != NULL) {
            const int size = strlen(load_data->items[idx].name) - strlen(ext) + 1;
            BLI_strncpy_rlen(load_data->items[idx].name, load_data->items[idx].name, size);
          }
        }
        else {
          /* More tokens, use first for `name` and save other data to be used later in the PDF
           * generation. In this case, all info received is used as is and not converted or
           * analyzed. */
          token = strtok_s(next_tokens, sep, &next_tokens);
          if (token) {
            BLI_strncpy(load_data->items[idx].name, token, sizeof(load_data->items[idx].name) - 1);
          }
          if (next_tokens) {
            BLI_strncpy(
                load_data->items[idx].data, next_tokens, sizeof(load_data->items[idx].data) - 1);
          }
        }

        MEM_SAFE_FREE(filename);
        idx++;
      }
      RNA_PROP_END;
    }
    MEM_freeN(directory);
  }
}

static void wm_contact_sheet_pdf_cancel(bContext *UNUSED(C), wmOperator *op)
{
  if (op->customdata) {
    ContactSheetParams *load_data = (ContactSheetParams *)op->customdata;
    MEM_SAFE_FREE(load_data->items);
    MEM_SAFE_FREE(op->customdata);
  }
}

static int wm_contact_sheet_pdf_exec(bContext *C, wmOperator *op)
{
  ContactSheetParams *load_data = MEM_callocN(sizeof(ContactSheetParams), __func__);
  load_data->len = RNA_property_collection_length(op->ptr,
                                                  RNA_struct_find_property(op->ptr, "files"));
  load_data->items = NULL;

  if (load_data->len == 0) {
    wm_contact_sheet_pdf_cancel(C, op);
    return OPERATOR_CANCELLED;
  }

  load_data->items = (ContactSheetItem *)MEM_malloc_arrayN(
      load_data->len, sizeof(ContactSheetItem), __func__);

  contact_sheet_pdf_load_files(C, op, load_data);

  const int orientation = RNA_enum_get(op->ptr, "orientation");
  load_data->page_size[0] = 3840;
  load_data->page_size[1] = 2160;
  if (orientation == 1) {
    SWAP(int32_t, load_data->page_size[0], load_data->page_size[1]);
  }

  load_data->rows = RNA_int_get(op->ptr, "rows");
  load_data->cols = RNA_int_get(op->ptr, "columns");

  RNA_string_get(op->ptr, "title", load_data->title);
  RNA_string_get(op->ptr, "logo_image", load_data->logo_image);

  op->customdata = load_data;
  WM_cursor_wait(true);
  bool done = create_contact_sheet_pdf(C, load_data);
  WM_cursor_wait(false);

  /* Free custom data. */
  wm_contact_sheet_pdf_cancel(C, op);

  if (!done) {
    BKE_report(op->reports, RPT_WARNING, "Unable to create Contact Sheet");
  }

  return OPERATOR_FINISHED;
}

static int wm_contact_sheet_pdf_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
  Scene *scene = CTX_data_scene(C);
  ED_fileselect_ensure_default_filepath(C, op, ".pdf");
  PropertyRNA *prop = RNA_struct_find_property(op->ptr, "title");
  if (!RNA_property_is_set(op->ptr, prop)) {
    RNA_string_set(op->ptr, "title", scene->id.name + 2);
  }

  WM_event_add_fileselect(C, op);

  return OPERATOR_RUNNING_MODAL;
}

static bool wm_contact_sheet_pdf_poll(bContext *C)
{
  if (CTX_wm_window(C) == NULL) {
    return false;
  }

  return true;
}

static void wm_contact_sheet_pdf_draw(bContext *UNUSED(C), wmOperator *op)
{
  uiLayout *layout = op->layout;
  PointerRNA *imfptr = op->ptr;
  uiLayout *row;

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  row = uiLayoutRow(layout, false);
  uiItemR(row, imfptr, "orientation", 0, NULL, ICON_NONE);

  row = uiLayoutRow(layout, false);
  uiItemR(row, imfptr, "columns", 0, NULL, ICON_NONE);
  row = uiLayoutRow(layout, false);
  uiItemR(row, imfptr, "rows", 0, NULL, ICON_NONE);
  row = uiLayoutRow(layout, false);
  uiItemR(row, imfptr, "title", 0, NULL, ICON_NONE);
}

void WM_OT_contact_sheet_pdf(struct wmOperatorType *ot)
{
  static const EnumPropertyItem prop_orientation_types[] = {
      {0, "HORIZONTAL", 0, "Setup the page in Landscape orientation", ""},
      {1, "VERTICAL", 0, "Setup the page in Portrait orientation", ""},
      {0, NULL, 0, NULL, NULL},
  };

  /* Identifiers. */
  ot->name = "Create Contact Sheet";
  ot->idname = "WM_OT_contact_sheet_pdf";
  ot->description = "Create a PDF with images as contact sheet";

  /* Api callbacks. */
  ot->invoke = wm_contact_sheet_pdf_invoke;
  ot->exec = wm_contact_sheet_pdf_exec;
  ot->cancel = wm_contact_sheet_pdf_cancel;
  ot->ui = wm_contact_sheet_pdf_draw;
  ot->poll = wm_contact_sheet_pdf_poll;

  /* Flags. */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  WM_operator_properties_filesel(ot,
                                 FILE_TYPE_FOLDER | FILE_TYPE_IMAGE,
                                 FILE_SPECIAL,
                                 FILE_OPENFILE,
                                 WM_FILESEL_DIRECTORY | WM_FILESEL_FILES | WM_FILESEL_SHOW_PROPS |
                                     WM_FILESEL_DIRECTORY | WM_FILESEL_FILEPATH,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_DEFAULT);
  /* Properties */
  RNA_def_int(ot->srna, "rows", 2, 1, 20, "Rows", "Number of rows by page", 1, 20);
  RNA_def_int(ot->srna, "columns", 3, 1, 20, "Columns", "Number of columns by page", 1, 20);
  RNA_def_string(ot->srna, "title", NULL, 128 - 2, "Title", "Title of the contact sheet");
  RNA_def_enum(ot->srna,
               "orientation",
               prop_orientation_types,
               0,
               "Orientation",
               "Orientation of the PDF page");
  RNA_def_string(
      ot->srna, "logo_image", NULL, 256, "Logo Image", "Path for an optional logo image");
}
#  endif /* WITH_HARU */

#endif /* WITH_IO_GPENCIL */
