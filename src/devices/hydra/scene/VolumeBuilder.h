// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Volume authoring (SPEC §7). An ANARI volume becomes four Hydra prims:
//
//  - a field bprim (type `openvdbAsset`, the stock Storm field type) per
//    spatial field, whose `filePath` is the field's
//    `anari://field/…​.anari` asset: AnariField.cpp's Hio plugin serves the
//    dense grid to Storm's field-texture pipeline by extension, no OpenVDB
//    involved;
//  - a second field bprim per volume serving its classification table
//    (transfer-function tables, value range, unit distance, field domain;
//    BuildClassificationTable) through the same plugin;
//  - a material sprim whose `volume` terminal is one fixed glslfx shader,
//    registered with Sdr from source; HwFieldReader_float nodes feed it the
//    field sample and bind the table;
//  - the volume rprim binding both fields (volumeFieldBinding `value` and
//    `tf`) and the material.
//
// The classification lives in a texture rather than in shader source because
// Sdr never frees a node and Storm refreshes a volume material's parameters
// only when its source changes (HdStMaterial::Sync): baking it into source
// cost a new Sdr node and a shader compile per edit (follow-up 019-5).

#pragma once

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/usd/sdf/path.h>

#include <memory>

namespace anari_hydra {

struct FieldSnapshot;
struct AnariFieldData;
struct VolumeClassification;

// The Hydra prim type of field bprims.
const pxr::TfToken &FieldPrimType();

// Field bprim naming `field`'s asset.
pxr::HdContainerDataSourceHandle BuildFieldPrim(const FieldSnapshot &field);

// Classification table for `c` over `field`, served as a field asset (see
// AnariFieldData::table).
std::shared_ptr<AnariFieldData> BuildClassificationTable(
    const VolumeClassification &c, const FieldSnapshot &field);

// The volume material sprim. It is the same for every volume and every
// classification.
pxr::HdContainerDataSourceHandle BuildVolumeMaterialPrim();

// Volume rprim bound to `fieldPath`, its classification table `tablePath` and
// `materialPath`. A non-empty `instancerPath` makes it a prototype of that
// instancer (SPEC §4.3).
pxr::HdContainerDataSourceHandle BuildVolumePrim(const VolumeClassification &c,
    const FieldSnapshot &field,
    const pxr::SdfPath &fieldPath,
    const pxr::SdfPath &tablePath,
    const pxr::SdfPath &materialPath,
    const pxr::SdfPath &instancerPath,
    const pxr::SdfPath &prototypeRoot);

} // namespace anari_hydra
