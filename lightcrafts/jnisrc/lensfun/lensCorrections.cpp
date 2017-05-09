/* Copyright (C) 2015- Masahiro Kitagawa */

#include <cmath>
#include <functional>
#include <jni.h>
#include <omp.h>
#ifndef AUTO_DEP
#include "javah/com_lightcrafts_jai_opimage_DistortionOpImage.h"
#endif

#include "LC_JNIUtils.h"

#include "interpolation.h"

std::function<float(float)> makeCoeff(int distModelType, const float* k)
{
    switch (distModelType) {
    case 0:
        return [](float radiusSq){
            return 1;
        };
    case 1: // 3rd order polynomial distortion model
        return [k](float radiusSq){
            return (1 - k[0] + k[0] * radiusSq);
        };
    case 2: // 5th order polynomial distortion model
        return [k](float radiusSq){
            return (1 + k[0] * radiusSq + k[1] * radiusSq * radiusSq);
        };
    case 3: // PTLens distortion model
        return [k](float radiusSq){
            const float radius = sqrt(radiusSq);
            return (k[0] * radiusSq * radius + k[1] * radiusSq + k[2] * radius
                    + 1 - k[0] - k[1] - k[2]);
        };
    case 4: // Adobe distortion model
        return [](float radiusSq){
            // TODO:
            return 1;
        };
    default: // LightZone's scaled 5th order polynomial distortion model
        return [k](float radiusSq){
            return (1 + k[0] * radiusSq + k[1] * radiusSq * radiusSq) / (1 + k[0] + k[1]);
        };
    }
}

void correct_distortion_mono
( const unsigned short *srcData, unsigned short *dstData,
  const int fullWidth, const int fullHeight,
  const int centerX, const int centerY,
  const int srcRectX, const int srcRectY,
  const int srcRectWidth, const int srcRectHeight,
  const int dstRectX, const int dstRectY,
  const int dstRectWidth, const int dstRectHeight,
  const int srcPixelStride, const int dstPixelStride,
  const int srcOffset, const int dstOffset,
  const int srcLineStride, const int dstLineStride,
  std::function<float(float)> coeff,
  const float magnitude )
{
    const float maxRadiusSq = (fullWidth * fullWidth + fullHeight * fullHeight) / 4.0;

#pragma omp parallel for schedule (guided)
    for (int y = dstRectY; y < dstRectY + dstRectHeight; ++y) {
        const float offY = y - centerY;

        for (int x = dstRectX; x < dstRectX + dstRectWidth; ++x) {
            // Calc distortion
            const float offX = x - centerX;
            const float radiusSq = (offX * offX + offY * offY) / maxRadiusSq;
            const float c = coeff(radiusSq);

            const float srcX = magnitude * c * offX + centerX - srcRectX;
            const float srcY = magnitude * c * offY + centerY - srcRectY;

            const int dstIdx =
                dstPixelStride * (x - dstRectX) + (y - dstRectY) * dstLineStride;

            if (srcX < 0 || srcX >= srcRectWidth || srcY < 0 || srcY >= srcRectHeight) {
                dstData[dstIdx + dstOffset] = 0;
            }
            else {
                const unsigned short value =
                    BilinearInterp(srcData, srcPixelStride, srcOffset, srcLineStride,
                                   srcX, srcY);
                dstData[dstIdx + dstOffset] = value < 0xffff ? value : 0xffff;
            }
        }
    }
}

extern "C"
JNIEXPORT void JNICALL Java_com_lightcrafts_jai_opimage_DistortionOpImage_distortionMono
( JNIEnv *env, jclass cls,
  jshortArray jsrcData, jshortArray jdstData,
  jint fullWidth, jint fullHeight,
  jint centerX, jint centerY,
  jint srcRectX, jint srcRectY, jint srcRectWidth, jint srcRectHeight,
  jint dstRectX, jint dstRectY, jint dstRectWidth, jint dstRectHeight,
  jint srcPixelStride, jint dstPixelStride,
  jint srcOffset, jint dstOffset,
  jint srcLineStride, jint dstLineStride,
  jint distModelType, jfloatArray jDistTerms )
{
    unsigned short *srcData = (unsigned short *)env->GetPrimitiveArrayCritical(jsrcData, 0);
    unsigned short *dstData = (unsigned short *)env->GetPrimitiveArrayCritical(jdstData, 0);
    jfloat* distTerms = env->GetFloatArrayElements(jDistTerms, 0);

    const float k[] = {distTerms[0], distTerms[1], distTerms[2]};
    auto coeff = makeCoeff(distModelType, k);

    correct_distortion_mono(srcData, dstData, fullWidth, fullHeight, centerX, centerY,
            srcRectX, srcRectY, srcRectWidth, srcRectHeight,
            dstRectX, dstRectY, dstRectWidth, dstRectHeight,
            srcPixelStride,dstPixelStride,
            srcOffset, dstOffset, srcLineStride, dstLineStride,
            coeff, 1.f);

    env->ReleaseFloatArrayElements(jDistTerms, distTerms, 0);
    env->ReleasePrimitiveArrayCritical(jsrcData, srcData, 0);
    env->ReleasePrimitiveArrayCritical(jdstData, dstData, 0);
}

extern "C"
JNIEXPORT void JNICALL Java_com_lightcrafts_jai_opimage_DistortionOpImage_distortionColor
( JNIEnv *env, jclass cls,
  jshortArray jsrcData, jshortArray jdstData,
  jint fullWidth, jint fullHeight,
  jint centerX, jint centerY,
  jint srcRectX, jint srcRectY, jint srcRectWidth, jint srcRectHeight,
  jint dstRectX, jint dstRectY, jint dstRectWidth, jint dstRectHeight,
  jint srcPixelStride, jint dstPixelStride,
  jint srcROffset, jint srcGOffset, jint srcBOffset,
  jint dstROffset, jint dstGOffset, jint dstBOffset,
  jint srcLineStride, jint dstLineStride,
  jint distModelType, jfloatArray jDistTerms,
  jfloatArray jTcaTerms )
{
    unsigned short *srcData = (unsigned short *)env->GetPrimitiveArrayCritical(jsrcData, 0);
    unsigned short *dstData = (unsigned short *)env->GetPrimitiveArrayCritical(jdstData, 0);

    jfloat*  tcaTerms = env->GetFloatArrayElements(jTcaTerms, 0);
    const float kr = tcaTerms[0];
    const float kb = tcaTerms[1];

    jfloat* distTerms = env->GetFloatArrayElements(jDistTerms, 0);
    const float k[] = {distTerms[0], distTerms[1], distTerms[2]};
    auto coeff = makeCoeff(distModelType, k);

#pragma omp parallel shared (distModel)
#pragma omp for single nowait
    {
        // Red
#pragma omp task mergable
        correct_distortion_mono(srcData, dstData, fullWidth, fullHeight, centerX, centerY,
                srcRectX, srcRectY, srcRectWidth, srcRectHeight,
                dstRectX, dstRectY, dstRectWidth, dstRectHeight,
                srcPixelStride,dstPixelStride,
                srcROffset, dstROffset, srcLineStride, dstLineStride,
                coeff, kr);

        // Green
#pragma omp task mergable
        correct_distortion_mono(srcData, dstData, fullWidth, fullHeight, centerX, centerY,
                srcRectX, srcRectY, srcRectWidth, srcRectHeight,
                dstRectX, dstRectY, dstRectWidth, dstRectHeight,
                srcPixelStride,dstPixelStride,
                srcGOffset, dstGOffset, srcLineStride, dstLineStride,
                coeff, 1);

        // Blue
#pragma omp task mergable
        correct_distortion_mono(srcData, dstData, fullWidth, fullHeight, centerX, centerY,
                srcRectX, srcRectY, srcRectWidth, srcRectHeight,
                dstRectX, dstRectY, dstRectWidth, dstRectHeight,
                srcPixelStride,dstPixelStride,
                srcBOffset, dstBOffset, srcLineStride, dstLineStride,
                coeff, kb);
    }

    env->ReleaseFloatArrayElements(jTcaTerms, tcaTerms, 0);
    env->ReleaseFloatArrayElements(jDistTerms, distTerms, 0);
    env->ReleasePrimitiveArrayCritical(jsrcData, srcData, 0);
    env->ReleasePrimitiveArrayCritical(jdstData, dstData, 0);
}

#include <iostream>
#include <lensfun.h>

inline const lfCamera* findCamera(JNIEnv *env, const lfDatabase* ldb,
        jstring cameraMakerStr, jstring cameraModelStr)
{
    const char *cameraMaker = env->GetStringUTFChars(cameraMakerStr, NULL);
    const char *cameraModel = env->GetStringUTFChars(cameraModelStr, NULL);
    const lfCamera **cameras = ldb->FindCamerasExt(cameraMaker, cameraModel);
    env->ReleaseStringUTFChars(cameraMakerStr, 0);
    env->ReleaseStringUTFChars(cameraModelStr, 0);

    if (!cameras) {
        std::cerr << "Cannot find the camera \""
            << cameraMaker << ": " << cameraModel << "\"" 
            << " in database" << std::endl;
        return nullptr;
    }
    const lfCamera *camera = cameras[0];
    lf_free(cameras);
    return camera;
}

inline const lfLens* findLenses(JNIEnv *env, const lfDatabase* ldb,
        const lfCamera* camera, jstring lensMakerStr, jstring lensModelStr)
{
    const char *lensMaker = env->GetStringUTFChars(lensMakerStr, NULL);
    const char *lensModel = env->GetStringUTFChars(lensModelStr, NULL);
    const lfLens **lenses = ldb->FindLenses(camera, lensMaker, lensModel);
    env->ReleaseStringUTFChars(lensMakerStr, 0);
    env->ReleaseStringUTFChars(lensModelStr, 0);

    if (!lenses) {
        std::cerr << "Cannot find the lens \""
            << lensMaker << ": " << lensModel << "\"";
        if (camera) {
            std::cerr << " for the camera \""
                << camera->Maker << ": " << camera->Model << "\"";
        }
        std::cerr << " in database" << std::endl;
        return nullptr;
    }

    // DEBUG
    for (int i = 0; lenses[i]; ++i) {
        std::cerr << "** lens" << i << " = "
            << lenses[i]->Maker << ": " << lenses[i]->Model << std::endl;
    }

    const lfLens *lens = lenses[0];
    lf_free(lenses);
    return lens;
}

static void applyModifier
( const lfModifier* mod,
  const unsigned short *srcData, unsigned short *dstData,
  const int srcRectX, const int srcRectY,
  const int srcRectWidth, const int srcRectHeight,
  const int dstRectX, const int dstRectY,
  const int dstRectWidth, const int dstRectHeight,
  const int srcPixelStride, const int dstPixelStride,
  const int srcROffset, const int srcGOffset, const int srcBOffset,
  const int dstROffset, const int dstGOffset, const int dstBOffset,
  const int srcLineStride, const int dstLineStride )
{
    float* pos = new float[dstRectWidth * 2 * 3];

#pragma omp parallel for schedule (guided)
    for (int y = dstRectY; y < dstRectY + dstRectHeight; ++y) {
        mod->ApplySubpixelGeometryDistortion(dstRectX, y, dstRectWidth, 1, pos);

        for (int x = dstRectX, i = 0; x < dstRectX + dstRectWidth; ++x, i += 6) {
            const float srcRX = pos[i];
            const float srcRY = pos[i + 1];
            const float srcGX = pos[i + 2];
            const float srcGY = pos[i + 3];
            const float srcBX = pos[i + 4];
            const float srcBY = pos[i + 5];

            const int dstIdx =
                dstPixelStride * (x - dstRectX) + (y - dstRectY) * dstLineStride;

            if (srcRX < srcRectX || srcRX >= srcRectX + srcRectWidth
             || srcRY < srcRectY || srcRY >= srcRectY + srcRectHeight
             || srcGX < srcRectX || srcGX >= srcRectX + srcRectWidth
             || srcGY < srcRectY || srcGY >= srcRectY + srcRectHeight
             || srcBX < srcRectX || srcBX >= srcRectX + srcRectWidth
             || srcBY < srcRectY || srcBY >= srcRectY + srcRectHeight) {
                dstData[dstIdx + dstROffset] = 0;
                dstData[dstIdx + dstGOffset] = 0;
                dstData[dstIdx + dstBOffset] = 0;
            }
            else {
                const unsigned short valueR = BilinearInterp(
                        srcData, srcPixelStride, srcROffset, srcLineStride,
                        srcRX - srcRectX, srcRY - srcRectY);
                const unsigned short valueG = BilinearInterp(
                        srcData, srcPixelStride, srcGOffset, srcLineStride,
                        srcGX - srcRectX, srcGY - srcRectY);
                const unsigned short valueB = BilinearInterp(
                        srcData, srcPixelStride, srcBOffset, srcLineStride,
                        srcBX - srcRectX, srcBY - srcRectY);
                dstData[dstIdx + dstROffset] = valueR < 0xffff ? valueR : 0xffff;
                dstData[dstIdx + dstGOffset] = valueG < 0xffff ? valueG : 0xffff;
                dstData[dstIdx + dstBOffset] = valueB < 0xffff ? valueB : 0xffff;
            }
        }
    }

    delete[] pos;
}

extern "C"
JNIEXPORT void JNICALL Java_com_lightcrafts_jai_opimage_DistortionOpImage_distortionColorLF
( JNIEnv *env, jclass cls,
  jshortArray jsrcData, jshortArray jdstData,
  jint fullWidth, jint fullHeight,
  jint centerX, jint centerY,
  jint srcRectX, jint srcRectY, jint srcRectWidth, jint srcRectHeight,
  jint dstRectX, jint dstRectY, jint dstRectWidth, jint dstRectHeight,
  jint srcPixelStride, jint dstPixelStride,
  jint srcROffset, jint srcGOffset, jint srcBOffset,
  jint dstROffset, jint dstGOffset, jint dstBOffset,
  jint srcLineStride, jint dstLineStride,
  jstring cameraMakerStr, jstring cameraModelStr,
  jstring lensMakerStr, jstring lensModelStr,
  jfloat focal, jfloat aperture )
{
    // Load lensfun database
    lfDatabase *ldb = lf_db_new();
    if (ldb->Load() != LF_NO_ERROR) {
        ldb->Destroy();
        return;
    }

    const lfCamera *camera = findCamera(env, ldb, cameraMakerStr, cameraModelStr);
    const lfLens *lens = findLenses(env, ldb, camera, lensMakerStr, lensModelStr);
    if (!lens) {
        ldb->Destroy();
        return;
    }

    const float crop = camera ? camera->CropFactor : lens->CropFactor;
    if (focal < 0.1f) {
        focal = lens->MaxFocal;
    }
    if (aperture < 0.1f) {
        aperture = lens->MinAperture;
    }
    constexpr float distance = 10; // TODO:
    constexpr float scale = 0; // automatic scaling
    const lfLensType targeom = lens->Type;

    lfModifier *mod = new lfModifier(lens, crop, fullWidth, fullHeight);
    if (!mod) {
        return;
    }
    mod->Initialize(lens, LF_PF_U16, focal, aperture, distance, scale, targeom,
            LF_MODIFY_ALL, false);

    unsigned short *srcData = (unsigned short *)env->GetPrimitiveArrayCritical(jsrcData, 0);
    unsigned short *dstData = (unsigned short *)env->GetPrimitiveArrayCritical(jdstData, 0);

    applyModifier(mod, srcData, dstData,
            srcRectX, srcRectY,
            srcRectWidth, srcRectHeight,
            dstRectX, dstRectY,
            dstRectWidth, dstRectHeight,
            srcPixelStride, dstPixelStride,
            srcROffset, srcGOffset, srcBOffset,
            dstROffset, dstGOffset, dstBOffset,
            srcLineStride, dstLineStride);

    delete mod;
    env->ReleasePrimitiveArrayCritical(jsrcData, srcData, 0);
    env->ReleasePrimitiveArrayCritical(jdstData, dstData, 0);
}

