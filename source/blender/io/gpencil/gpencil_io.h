/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2020 Blender Foundation. All rights reserved. */
#pragma once

/** \file
 * \ingroup bgpencil
 */

#ifdef __cplusplus
extern "C" {
#endif

struct ARegion;
struct Object;
struct View3D;
struct bContext;

typedef struct GpencilIOParams {
  bContext *C;
  ARegion *region;
  View3D *v3d;
  /** Grease pencil object. */
  Object *ob;
  /** Mode (see eGpencilIO_Modes). */
  uint16_t mode;
  int32_t frame_start;
  int32_t frame_end;
  int32_t frame_cur;
  uint32_t flag;
  float scale;
  /** Select mode (see eGpencilExportSelect). */
  uint16_t select_mode;
  /** Frame mode (see eGpencilExportFrame). */
  uint16_t frame_mode;
  /** Stroke sampling factor. */
  float stroke_sample;
  int32_t resolution;
  /** Filename to be used in new objects. */
  char filename[128];
} GpencilIOParams;

/* GpencilIOParams->flag. */
typedef enum eGpencilIOParams_Flag {
  /* Export Filled strokes. */
  GP_EXPORT_FILL = (1 << 0),
  /* Export normalized thickness. */
  GP_EXPORT_NORM_THICKNESS = (1 << 1),
  /* Clip camera area. */
  GP_EXPORT_CLIP_CAMERA = (1 << 2),
} eGpencilIOParams_Flag;

typedef enum eGpencilIO_Modes {
  GP_EXPORT_TO_SVG = 0,
  GP_EXPORT_TO_PDF = 1,

  GP_IMPORT_FROM_SVG = 2,
  /* Add new formats here. */
} eGpencilIO_Modes;

/* Object to be exported. */
typedef enum eGpencilExportSelect {
  GP_EXPORT_ACTIVE = 0,
  GP_EXPORT_SELECTED = 1,
  GP_EXPORT_VISIBLE = 2,
} eGpencilExportSelect;

/** Frame-range to be exported. */
typedef enum eGpencilExportFrame {
  GP_EXPORT_FRAME_ACTIVE = 0,
  GP_EXPORT_FRAME_SELECTED = 1,
  GP_EXPORT_FRAME_SCENE = 2,
} eGpencilExportFrame;

/** Contact Sheet load data. */
typedef struct ContactSheetItem {
  char name[64];   /* name. */
  char path[1024]; /* 1024 = FILE_MAX */
  /** Extra info data separated by pipes (|). */
  char data[2048];
} ContactSheetItem;

typedef struct ContactSheetParams {

  char outpath[1024]; /* 1024 = FILE_MAX */
  uint32_t len;

  uint32_t page_size[2];
  uint32_t rows, cols;

  char title[128];
  char logo_image[256];

  ContactSheetItem *items;
} ContactSheetParams;

/**
 * Main export entry point function.
 */
bool gpencil_io_export(const char *filepath, struct GpencilIOParams *iparams);
/**
 * Main import entry point function.
 */
bool gpencil_io_import(const char *filepath, struct GpencilIOParams *iparams);

/**
 * Create Contact Sheet in PDF.
 */
bool create_contact_sheet_pdf(bContext *C, ContactSheetParams *iparams);

#ifdef __cplusplus
}
#endif
