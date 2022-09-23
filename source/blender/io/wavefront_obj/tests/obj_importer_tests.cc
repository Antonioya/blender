/* SPDX-License-Identifier: Apache-2.0 */

#include <gtest/gtest.h>

#include "testing/testing.h"
#include "tests/blendfile_loading_base_test.h"

#include "BKE_curve.h"
#include "BKE_customdata.h"
#include "BKE_main.h"
#include "BKE_mesh.h"
#include "BKE_object.h"
#include "BKE_scene.h"

#include "BLI_listbase.h"
#include "BLI_math_base.hh"
#include "BLI_math_vec_types.hh"

#include "BLO_readfile.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "DNA_curve_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_scene_types.h"

#include "MEM_guardedalloc.h"

#include "obj_importer.hh"

namespace blender::io::obj {

struct Expectation {
  std::string name;
  short type; /* OB_MESH, ... */
  int totvert, mesh_totedge_or_curve_endp, mesh_totpoly_or_curve_order,
      mesh_totloop_or_curve_cyclic;
  float3 vert_first, vert_last;
  float3 normal_first;
  float2 uv_first;
  float4 color_first = {-1, -1, -1, -1};
};

class obj_importer_test : public BlendfileLoadingBaseTest {
 public:
  void import_and_check(const char *path,
                        const Expectation *expect,
                        size_t expect_count,
                        int expect_mat_count,
                        int expect_image_count = 0)
  {
    if (!blendfile_load("io_tests/blend_geometry/all_quads.blend")) {
      ADD_FAILURE();
      return;
    }

    OBJImportParams params;
    params.clamp_size = 0;
    params.forward_axis = IO_AXIS_NEGATIVE_Z;
    params.up_axis = IO_AXIS_Y;
    params.validate_meshes = true;
    params.import_vertex_groups = false;
    params.relative_paths = true;
    params.clear_selection = true;

    std::string obj_path = blender::tests::flags_test_asset_dir() + "/io_tests/obj/" + path;
    strncpy(params.filepath, obj_path.c_str(), FILE_MAX - 1);
    const size_t read_buffer_size = 650;
    importer_main(bfile->main, bfile->curscene, bfile->cur_view_layer, params, read_buffer_size);

    depsgraph_create(DAG_EVAL_VIEWPORT);

    DEGObjectIterSettings deg_iter_settings{};
    deg_iter_settings.depsgraph = depsgraph;
    deg_iter_settings.flags = DEG_ITER_OBJECT_FLAG_LINKED_DIRECTLY |
                              DEG_ITER_OBJECT_FLAG_LINKED_VIA_SET | DEG_ITER_OBJECT_FLAG_VISIBLE |
                              DEG_ITER_OBJECT_FLAG_DUPLI;
    size_t object_index = 0;
    DEG_OBJECT_ITER_BEGIN (&deg_iter_settings, object) {
      if (object_index >= expect_count) {
        ADD_FAILURE();
        break;
      }
      const Expectation &exp = expect[object_index];
      ASSERT_STREQ(object->id.name, exp.name.c_str());
      EXPECT_EQ(object->type, exp.type);
      EXPECT_V3_NEAR(object->loc, float3(0, 0, 0), 0.0001f);
      if (strcmp(object->id.name, "OBCube") != 0) {
        EXPECT_V3_NEAR(object->rot, float3(M_PI_2, 0, 0), 0.0001f);
      }
      EXPECT_V3_NEAR(object->scale, float3(1, 1, 1), 0.0001f);
      if (object->type == OB_MESH) {
        Mesh *mesh = BKE_object_get_evaluated_mesh(object);
        EXPECT_EQ(mesh->totvert, exp.totvert);
        EXPECT_EQ(mesh->totedge, exp.mesh_totedge_or_curve_endp);
        EXPECT_EQ(mesh->totpoly, exp.mesh_totpoly_or_curve_order);
        EXPECT_EQ(mesh->totloop, exp.mesh_totloop_or_curve_cyclic);
        const Span<MVert> verts = mesh->verts();
        EXPECT_V3_NEAR(verts.first().co, exp.vert_first, 0.0001f);
        EXPECT_V3_NEAR(verts.last().co, exp.vert_last, 0.0001f);
        const float3 *lnors = (const float3 *)(CustomData_get_layer(&mesh->ldata, CD_NORMAL));
        float3 normal_first = lnors != nullptr ? lnors[0] : float3(0, 0, 0);
        EXPECT_V3_NEAR(normal_first, exp.normal_first, 0.0001f);
        const MLoopUV *mloopuv = static_cast<const MLoopUV *>(
            CustomData_get_layer(&mesh->ldata, CD_MLOOPUV));
        float2 uv_first = mloopuv ? float2(mloopuv->uv) : float2(0, 0);
        EXPECT_V2_NEAR(uv_first, exp.uv_first, 0.0001f);
        if (exp.color_first.x >= 0) {
          const float4 *colors = (const float4 *)(CustomData_get_layer(&mesh->vdata,
                                                                       CD_PROP_COLOR));
          EXPECT_TRUE(colors != nullptr);
          EXPECT_V4_NEAR(colors[0], exp.color_first, 0.0001f);
        }
        else {
          EXPECT_FALSE(CustomData_has_layer(&mesh->vdata, CD_PROP_COLOR));
        }
      }
      if (object->type == OB_CURVES_LEGACY) {
        Curve *curve = static_cast<Curve *>(DEG_get_evaluated_object(depsgraph, object)->data);
        int numVerts;
        float(*vertexCos)[3] = BKE_curve_nurbs_vert_coords_alloc(&curve->nurb, &numVerts);
        EXPECT_EQ(numVerts, exp.totvert);
        EXPECT_V3_NEAR(vertexCos[0], exp.vert_first, 0.0001f);
        EXPECT_V3_NEAR(vertexCos[numVerts - 1], exp.vert_last, 0.0001f);
        MEM_freeN(vertexCos);
        const Nurb *nurb = static_cast<const Nurb *>(BLI_findlink(&curve->nurb, 0));
        int endpoint = (nurb->flagu & CU_NURB_ENDPOINT) ? 1 : 0;
        EXPECT_EQ(nurb->orderu, exp.mesh_totpoly_or_curve_order);
        EXPECT_EQ(endpoint, exp.mesh_totedge_or_curve_endp);
        /* Cyclic flag is not set by the importer yet. */
        // int cyclic = (nurb->flagu & CU_NURB_CYCLIC) ? 1 : 0;
        // EXPECT_EQ(cyclic, exp.mesh_totloop_or_curve_cyclic);
      }
      ++object_index;
    }
    DEG_OBJECT_ITER_END;
    EXPECT_EQ(object_index, expect_count);

    /* Check number of materials & textures. */
    const int mat_count = BLI_listbase_count(&bfile->main->materials);
    EXPECT_EQ(mat_count, expect_mat_count);

    const int ima_count = BLI_listbase_count(&bfile->main->images);
    EXPECT_EQ(ima_count, expect_image_count);
  }
};

TEST_F(obj_importer_test, import_cube)
{
  Expectation expect[] = {
      {"OBCube", OB_MESH, 8, 12, 6, 24, float3(1, 1, -1), float3(-1, 1, 1)},
      {"OBcube",
       OB_MESH,
       8,
       12,
       6,
       24,
       float3(-1, -1, 1),
       float3(1, -1, -1),
       float3(-0.57735f, 0.57735f, -0.57735f)},
  };
  import_and_check("cube.obj", expect, std::size(expect), 1);
}

TEST_F(obj_importer_test, import_cube_o_after_verts)
{
  Expectation expect[] = {
      {"OBCube", OB_MESH, 8, 12, 6, 24, float3(1, 1, -1), float3(-1, 1, 1)},
      {
          "OBActualCube",
          OB_MESH,
          8,
          12,
          6,
          24,
          float3(-1, -1, 1),
          float3(1, -1, -1),
          float3(0, 0, 1),
      },
      {
          "OBSparseTri",
          OB_MESH,
          3,
          3,
          1,
          3,
          float3(1, -1, 1),
          float3(-2, -2, 2),
          float3(-0.2357f, 0.9428f, 0.2357f),
      },
  };
  import_and_check("cube_o_after_verts.obj", expect, std::size(expect), 2);
}

TEST_F(obj_importer_test, import_suzanne_all_data)
{
  Expectation expect[] = {
      {"OBCube", OB_MESH, 8, 12, 6, 24, float3(1, 1, -1), float3(-1, 1, 1)},
      {"OBMonkey",
       OB_MESH,
       505,
       1005,
       500,
       1968,
       float3(-0.4375f, 0.164062f, 0.765625f),
       float3(0.4375f, 0.164062f, 0.765625f),
       float3(-0.6040f, -0.5102f, 0.6122f),
       float2(0.692094f, 0.40191f)},
  };
  import_and_check("suzanne_all_data.obj", expect, std::size(expect), 0);
}

TEST_F(obj_importer_test, import_nurbs)
{
  Expectation expect[] = {
      {"OBCube", OB_MESH, 8, 12, 6, 24, float3(1, 1, -1), float3(-1, 1, 1)},
      {"OBnurbs",
       OB_CURVES_LEGACY,
       12,
       0,
       4,
       1,
       float3(0.260472f, -1.477212f, -0.866025f),
       float3(-1.5f, 2.598076f, 0)},
  };
  import_and_check("nurbs.obj", expect, std::size(expect), 0);
}

TEST_F(obj_importer_test, import_nurbs_curves)
{
  Expectation expect[] = {
      {"OBCube", OB_MESH, 8, 12, 6, 24, float3(1, 1, -1), float3(-1, 1, 1)},
      {"OBCurveDeg3", OB_CURVES_LEGACY, 4, 0, 3, 0, float3(10, -2, 0), float3(6, -2, 0)},
      {"OBnurbs_curves", OB_CURVES_LEGACY, 4, 0, 4, 0, float3(2, -2, 0), float3(-2, -2, 0)},
      {"OBNurbsCurveCyclic", OB_CURVES_LEGACY, 7, 0, 4, 1, float3(-2, -2, 0), float3(-6, 2, 0)},
      {"OBNurbsCurveDiffWeights",
       OB_CURVES_LEGACY,
       4,
       0,
       4,
       0,
       float3(6, -2, 0),
       float3(2, -2, 0)},
      {"OBNurbsCurveEndpoint",
       OB_CURVES_LEGACY,
       4,
       1,
       4,
       0,
       float3(-6, -2, 0),
       float3(-10, -2, 0)},
  };
  import_and_check("nurbs_curves.obj", expect, std::size(expect), 0);
}

TEST_F(obj_importer_test, import_nurbs_cyclic)
{
  Expectation expect[] = {
      {"OBCube", OB_MESH, 8, 12, 6, 24, float3(1, 1, -1), float3(-1, 1, 1)},
      {"OBnurbs_cyclic",
       OB_CURVES_LEGACY,
       31,
       0,
       4,
       1,
       float3(2.591002f, 0, -0.794829f),
       float3(3.280729f, 0, 3.043217f)},
  };
  import_and_check("nurbs_cyclic.obj", expect, std::size(expect), 0);
}

TEST_F(obj_importer_test, import_nurbs_manual)
{
  Expectation expect[] = {
      {"OBCube", OB_MESH, 8, 12, 6, 24, float3(1, 1, -1), float3(-1, 1, 1)},
      {"OBCurve_Cyclic", OB_CURVES_LEGACY, 7, 0, 4, 1, float3(-2, 0, 2), float3(2, 0, -2)},
      {"OBCurve_Endpoints", OB_CURVES_LEGACY, 5, 1, 4, 0, float3(-2, 0, 2), float3(-2, 0, 2)},
      {"OBCurve_NonUniform_Parm",
       OB_CURVES_LEGACY,
       5,
       0,
       4,
       0,
       float3(-2, 0, 2),
       float3(-2, 0, 2)},
      {"OBCurve_Uniform_Parm", OB_CURVES_LEGACY, 5, 0, 4, 0, float3(-2, 0, 2), float3(-2, 0, 2)},
  };
  import_and_check("nurbs_manual.obj", expect, std::size(expect), 0);
}

TEST_F(obj_importer_test, import_nurbs_mesh)
{
  Expectation expect[] = {
      {"OBCube", OB_MESH, 8, 12, 6, 24, float3(1, 1, -1), float3(-1, 1, 1)},
      {"OBTorus_Knot",
       OB_MESH,
       108,
       108,
       0,
       0,
       float3(0.438725f, 1.070313f, 0.433013f),
       float3(0.625557f, 1.040691f, 0.460328f)},
  };
  import_and_check("nurbs_mesh.obj", expect, std::size(expect), 0);
}

TEST_F(obj_importer_test, import_materials)
{
  Expectation expect[] = {
      {"OBCube", OB_MESH, 8, 12, 6, 24, float3(1, 1, -1), float3(-1, 1, 1)},
      {"OBmaterials", OB_MESH, 8, 12, 6, 24, float3(-1, -1, 1), float3(1, -1, -1)},
  };
  import_and_check("materials.obj", expect, std::size(expect), 4, 8);
}

TEST_F(obj_importer_test, import_cubes_with_textures_rel)
{
  Expectation expect[] = {
      {"OBCube", OB_MESH, 8, 12, 6, 24, float3(1, 1, -1), float3(-1, 1, 1)},
      {"OBCube4Tex",
       OB_MESH,
       8,
       12,
       6,
       24,
       float3(1, 1, -1),
       float3(-1, -1, 1),
       float3(0, 1, 0),
       float2(0.9935f, 0.0020f)},
      {"OBCubeTexMul",
       OB_MESH,
       8,
       12,
       6,
       24,
       float3(4, -2, -1),
       float3(2, -4, 1),
       float3(0, 1, 0),
       float2(0.9935f, 0.0020f)},
      {"OBCubeTiledTex",
       OB_MESH,
       8,
       12,
       6,
       24,
       float3(4, 1, -1),
       float3(2, -1, 1),
       float3(0, 1, 0),
       float2(0.9935f, 0.0020f)},
      {"OBCubeTiledTexFromAnotherFolder",
       OB_MESH,
       8,
       12,
       6,
       24,
       float3(7, 1, -1),
       float3(5, -1, 1),
       float3(0, 1, 0),
       float2(0.9935f, 0.0020f)},
  };
  import_and_check("cubes_with_textures_rel.obj", expect, std::size(expect), 4, 4);
}

TEST_F(obj_importer_test, import_faces_invalid_or_with_holes)
{
  Expectation expect[] = {
      {"OBCube", OB_MESH, 8, 12, 6, 24, float3(1, 1, -1), float3(-1, 1, 1)},
      {"OBFaceAllVerts_BecomesOneOverlappingFaceUsingAllVerts",
       OB_MESH,
       8,
       8,
       1,
       8,
       float3(8, 0, -2),
       float3(11, 0, -1)},
      {"OBFaceAllVertsDup_BecomesOneOverlappingFaceUsingAllVerts",
       OB_MESH,
       8,
       8,
       1,
       8,
       float3(3, 0, 3),
       float3(6, 0, 4)},
      {"OBFaceJustTwoVerts_IsSkipped", OB_MESH, 2, 0, 0, 0, float3(8, 0, 3), float3(8, 0, 7)},
      {"OBFaceQuadDupSomeVerts_BecomesOneQuadUsing4Verts",
       OB_MESH,
       4,
       4,
       1,
       4,
       float3(3, 0, -2),
       float3(7, 0, -2)},
      {"OBFaceTriDupVert_Becomes1Tri", OB_MESH, 3, 3, 1, 3, float3(-2, 0, 3), float3(2, 0, 7)},
      {"OBFaceWithHole_BecomesTwoFacesFormingAHole",
       OB_MESH,
       8,
       10,
       2,
       12,
       float3(-2, 0, -2),
       float3(1, 0, -1)},
  };
  import_and_check("faces_invalid_or_with_holes.obj", expect, std::size(expect), 0);
}

TEST_F(obj_importer_test, import_invalid_indices)
{
  Expectation expect[] = {
      {"OBCube", OB_MESH, 8, 12, 6, 24, float3(1, 1, -1), float3(-1, 1, 1)},
      {"OBQuad",
       OB_MESH,
       3,
       3,
       1,
       3,
       float3(-2, 0, -2),
       float3(2, 0, 2),
       float3(0, 1, 0),
       float2(0.5f, 0.25f)},
  };
  import_and_check("invalid_indices.obj", expect, std::size(expect), 0);
}

TEST_F(obj_importer_test, import_invalid_syntax)
{
  Expectation expect[] = {
      {"OBCube", OB_MESH, 8, 12, 6, 24, float3(1, 1, -1), float3(-1, 1, 1)},
      {"OBObjectWithAReallyLongNameToCheckHowImportHandlesNamesThatAreLon",
       OB_MESH,
       3,
       3,
       1,
       3,
       float3(1, 2, 3),
       float3(7, 8, 9),
       float3(0, 1, 0),
       float2(0.5f, 0.25f)},
  };
  import_and_check("invalid_syntax.obj", expect, std::size(expect), 0);
}

TEST_F(obj_importer_test, import_all_objects)
{
  Expectation expect[] = {
      {"OBCube", OB_MESH, 8, 12, 6, 24, float3(1, 1, -1), float3(-1, 1, 1)},
      /* .obj file has empty EmptyText and EmptyMesh objects; these are ignored and skipped */
      {"OBBezierCurve", OB_MESH, 13, 12, 0, 0, float3(-1, -2, 0), float3(1, -2, 0)},
      {"OBBlankCube", OB_MESH, 8, 13, 7, 26, float3(1, 1, -1), float3(-1, 1, 1), float3(0, 0, 1)},
      {"OBMaterialCube",
       OB_MESH,
       8,
       13,
       7,
       26,
       float3(28, 1, -1),
       float3(26, 1, 1),
       float3(-1, 0, 0)},
      {"OBNurbsCircle",
       OB_MESH,
       96,
       96,
       0,
       0,
       float3(3.292893f, -2.707107f, 0),
       float3(3.369084f, -2.77607f, 0)},
      {"OBNurbsCircle.001", OB_MESH, 4, 4, 0, 0, float3(2, -3, 0), float3(3, -2, 0)},
      {"OBParticleCube",
       OB_MESH,
       8,
       13,
       7,
       26,
       float3(22, 1, -1),
       float3(20, 1, 1),
       float3(0, 0, 1)},
      {"OBShapeKeyCube",
       OB_MESH,
       8,
       13,
       7,
       26,
       float3(19, 1, -1),
       float3(17, 1, 1),
       float3(-0.4082f, -0.4082f, 0.8165f)},
      {"OBSmoothCube",
       OB_MESH,
       8,
       13,
       7,
       26,
       float3(4, 1, -1),
       float3(2, 1, 1),
       float3(0.5774f, 0.5773f, 0.5774f)},
      {"OBSurface",
       OB_MESH,
       256,
       480,
       224,
       896,
       float3(7.292893f, -2.707107f, -1),
       float3(7.525872f, -2.883338f, 1),
       float3(-0.7071f, -0.7071f, 0),
       float2(0, 0.142857f)},
      {"OBSurfPatch",
       OB_MESH,
       256,
       480,
       225,
       900,
       float3(12.5f, -2.5f, 0.694444f),
       float3(13.5f, -1.5f, 0.694444f),
       float3(-0.3246f, -0.3531f, 0.8775f),
       float2(0, 0.066667f)},
      {"OBSurfSphere",
       OB_MESH,
       640,
       1248,
       608,
       2432,
       float3(11, -2, -1),
       float3(11, -2, 1),
       float3(-0.0541f, -0.0541f, -0.9971f),
       float2(0, 1)},
      {"OBSurfTorus.001",
       OB_MESH,
       1024,
       2048,
       1024,
       4096,
       float3(5.34467f, -2.65533f, -0.176777f),
       float3(5.232792f, -2.411795f, -0.220835f),
       float3(-0.5042f, -0.5042f, -0.7011f),
       float2(0, 1)},
      {"OBTaperCube",
       OB_MESH,
       106,
       208,
       104,
       416,
       float3(24.444445f, 0.502543f, -0.753814f),
       float3(23.790743f, 0.460522f, -0.766546f),
       float3(-0.0546f, 0.1716f, 0.9837f)},
      {"OBText",
       OB_MESH,
       177,
       345,
       171,
       513,
       float3(1.75f, -9.458f, 0),
       float3(0.587f, -9.406f, 0),
       float3(0, 0, 1),
       float2(0.017544f, 0)},
      {"OBUVCube",
       OB_MESH,
       8,
       13,
       7,
       26,
       float3(7, 1, -1),
       float3(5, 1, 1),
       float3(0, 0, 1),
       float2(0.654526f, 0.579873f)},
      {"OBUVImageCube",
       OB_MESH,
       8,
       13,
       7,
       26,
       float3(10, 1, -1),
       float3(8, 1, 1),
       float3(0, 0, 1),
       float2(0.654526f, 0.579873f)},
      {"OBVColCube",
       OB_MESH,
       8,
       13,
       7,
       26,
       float3(13, 1, -1),
       float3(11, 1, 1),
       float3(0, 0, 1),
       float2(0, 0),
       float4(0.0f, 0.002125f, 1.0f, 1.0f)},
      {"OBVGroupCube",
       OB_MESH,
       8,
       13,
       7,
       26,
       float3(16, 1, -1),
       float3(14, 1, 1),
       float3(0, 0, 1)},
  };
  import_and_check("all_objects.obj", expect, std::size(expect), 7);
}

TEST_F(obj_importer_test, import_cubes_vertex_colors)
{
  Expectation expect[] = {
      {"OBCube", OB_MESH, 8, 12, 6, 24, float3(1, 1, -1), float3(-1, 1, 1)},
      {"OBCubeCornerByte",
       OB_MESH,
       8,
       12,
       6,
       24,
       float3(1.0f, 1.0f, -3.812445f),
       float3(-1.0f, -1.0f, -1.812445f),
       float3(0, 0, 0),
       float2(0, 0),
       float4(0.89627f, 0.036889f, 0.47932f, 1.0f)},
      {"OBCubeCornerFloat",
       OB_MESH,
       8,
       12,
       6,
       24,
       float3(3.481967f, 1.0f, -3.812445f),
       float3(1.481967f, -1.0f, -1.812445f),
       float3(0, 0, 0),
       float2(0, 0),
       float4(1.564582f, 0.039217f, 0.664309f, 1.0f)},
      {"OBCubeMultiColorAttribs",
       OB_MESH,
       8,
       12,
       6,
       24,
       float3(-4.725068f, -1.0f, 1.0f),
       float3(-2.725068f, 1.0f, -1.0f),
       float3(0, 0, 0),
       float2(0, 0),
       float4(0.270498f, 0.47932f, 0.262251f, 1.0f)},
      {"OBCubeNoColors",
       OB_MESH,
       8,
       12,
       6,
       24,
       float3(-4.550208f, -1.0f, -1.918042f),
       float3(-2.550208f, 1.0f, -3.918042f)},
      {"OBCubeVertexByte",
       OB_MESH,
       8,
       12,
       6,
       24,
       float3(1.0f, 1.0f, -1.0f),
       float3(-1.0f, -1.0f, 1.0f),
       float3(0, 0, 0),
       float2(0, 0),
       float4(0.846873f, 0.027321f, 0.982123f, 1.0f)},
      {"OBCubeVertexFloat",
       OB_MESH,
       8,
       12,
       6,
       24,
       float3(3.392028f, 1.0f, -1.0f),
       float3(1.392028f, -1.0f, 1.0f),
       float3(0, 0, 0),
       float2(0, 0),
       float4(49.99467f, 0.027321f, 0.982123f, 1.0f)},
  };
  import_and_check("cubes_vertex_colors.obj", expect, std::size(expect), 0);
}

TEST_F(obj_importer_test, import_cubes_vertex_colors_mrgb)
{
  Expectation expect[] = {
      {"OBCube", OB_MESH, 8, 12, 6, 24, float3(1, 1, -1), float3(-1, 1, 1)},
      {"OBCubeMRGB",
       OB_MESH,
       8,
       12,
       6,
       24,
       float3(4, 1, -1),
       float3(2, -1, 1),
       float3(0, 0, 0),
       float2(0, 0),
       float4(0.8714f, 0.6308f, 0.5271f, 1.0f)},
      {"OBCubeXYZRGB",
       OB_MESH,
       8,
       12,
       6,
       24,
       float3(1, 1, -1),
       float3(-1, -1, 1),
       float3(0, 0, 0),
       float2(0, 0),
       float4(0.6038f, 0.3185f, 0.1329f, 1.0f)},
      {"OBTriMRGB",
       OB_MESH,
       3,
       3,
       1,
       3,
       float3(12, 1, -1),
       float3(10, 0, -1),
       float3(0, 0, 0),
       float2(0, 0),
       float4(1.0f, 0.0f, 0.0f, 1.0f)},
      {
          "OBTriNoColors",
          OB_MESH,
          3,
          3,
          1,
          3,
          float3(8, 1, -1),
          float3(6, 0, -1),
      },
  };
  import_and_check("cubes_vertex_colors_mrgb.obj", expect, std::size(expect), 0);
}

TEST_F(obj_importer_test, import_vertices)
{
  Expectation expect[] = {
      {"OBCube", OB_MESH, 8, 12, 6, 24, float3(1, 1, -1), float3(-1, 1, 1)},
      /* Loose vertices without faces or edges. */
      {"OBCube.001", OB_MESH, 8, 0, 0, 0, float3(1, 1, -1), float3(-1, 1, 1)},
  };
  import_and_check("vertices.obj", expect, std::size(expect), 0);
}

}  // namespace blender::io::obj
