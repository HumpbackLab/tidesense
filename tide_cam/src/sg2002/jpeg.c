#include "jpeg.h"
#include <cvi_venc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct tide_jpeg {
    int channel[2], receiving[2], held[2];
    VENC_PACK_S pack[2];
    VENC_STREAM_S stream[2];
};

static int fail(char *error, const char *operation, int status)
{
    snprintf(error, TIDE_CAM_ERROR_SIZE, "%s failed (%#x)", operation, (unsigned)status);
    return TIDE_CAM_IO;
}

int tide_jpeg_release(tide_jpeg *j, char *error)
{
    int result = TIDE_CAM_OK;
    for (int p = 0; p < 2; ++p) {
        if (j->held[p]) {
            int ret = CVI_VENC_ReleaseStream(j->channel[p], &j->stream[p]);
            if (ret && !result) result = fail(error, "CVI_VENC_ReleaseStream", ret);
            j->held[p] = 0;
        }
        if (j->receiving[p]) {
            int ret = CVI_VENC_StopRecvFrame(j->channel[p]);
            if (ret && !result) result = fail(error, "CVI_VENC_StopRecvFrame", ret);
            j->receiving[p] = 0;
        }
    }
    return result;
}

int tide_jpeg_abort(tide_jpeg *j, char *error)
{
    if (!j) return TIDE_CAM_OK;
    int result = tide_jpeg_release(j, error);
    for (int p = 0; p < 2; ++p) if (j->channel[p] >= 0) {
        int ret = CVI_VENC_DestroyChn(j->channel[p]);
        if (ret && !result) result = fail(error, "CVI_VENC_DestroyChn", ret);
        if (!ret) j->channel[p] = -1;
    }
    return result;
}

void tide_jpeg_close(tide_jpeg *j)
{
    if (!j) return;
    char error[TIDE_CAM_ERROR_SIZE] = "";
    if (tide_jpeg_abort(j, error)) fprintf(stderr, "tide_cam: %s\n", error);
    free(j);
}

int tide_jpeg_open(const tide_cam_config *config, tide_jpeg **out, char *error)
{
    *out = NULL;
    VENC_PARAM_MOD_S module = { .enVencModType = MODTYPE_JPEGE };
    int module_status = CVI_VENC_GetModParam(&module);
    if (module_status) return fail(error, "CVI_VENC_GetModParam", module_status);
    /* Shared ES mode holds the global JPEG lock until ReleaseStream, so two
     * simultaneously borrowed channel outputs require separate ES buffers. */
    if (module.stJpegeModParam.bSingleEsBuf) {
        snprintf(error, TIDE_CAM_ERROR_SIZE, "Stereo JPEG requires separate encoder ES buffers (bSingleEsBuf=0)");
        return TIDE_CAM_UNSUPPORTED;
    }
    tide_jpeg *j = calloc(1, sizeof(*j));
    if (!j) return TIDE_CAM_NO_MEMORY;
    j->channel[0] = j->channel[1] = -1;
    for (int p = 0; p < 2; ++p) {
        VENC_CHN_ATTR_S attr = {0};
        attr.stVencAttr.enType = PT_JPEG;
        attr.stVencAttr.u32MaxPicWidth = attr.stVencAttr.u32PicWidth = config->mode.width;
        attr.stVencAttr.u32MaxPicHeight = attr.stVencAttr.u32PicHeight = config->mode.height;
        attr.stVencAttr.u32BufSize = config->mode.width * config->mode.height * 2;
        attr.stVencAttr.bByFrame = CVI_TRUE;
        attr.stVencAttr.stAttrJpege.enReceiveMode = VENC_PIC_RECEIVE_SINGLE;
        attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
        for (int ch = 0; ch < VENC_MAX_CHN_NUM; ++ch) {
            VENC_CHN_ATTR_S existing;
            if (!CVI_VENC_GetChnAttr(ch, &existing)) continue;
            int ret = CVI_VENC_CreateChn(ch, &attr);
            if (ret) { fail(error, "CVI_VENC_CreateChn", ret); goto failure; }
            j->channel[p] = ch;
            break;
        }
        if (j->channel[p] < 0) {
            snprintf(error, TIDE_CAM_ERROR_SIZE, "Two free JPEG encoder channels are required");
            goto failure;
        }
        VENC_JPEG_PARAM_S quality;
        int ret = CVI_VENC_GetJpegParam(j->channel[p], &quality);
        if (ret) { fail(error, "CVI_VENC_GetJpegParam", ret); goto failure; }
        quality.u32Qfactor = config->jpeg_quality;
        ret = CVI_VENC_SetJpegParam(j->channel[p], &quality);
        if (ret) { fail(error, "CVI_VENC_SetJpegParam", ret); goto failure; }
    }
    *out = j;
    return TIDE_CAM_OK;
failure:
    tide_jpeg_close(j);
    return TIDE_CAM_IO;
}

int tide_jpeg_encode(tide_jpeg *j, const VIDEO_FRAME_INFO_S input[2],
                     tide_cam_jpeg_pair *pair, char *error)
{
    int status = TIDE_CAM_OK;
    for (int p = 0; p < 2; ++p) {
        VENC_RECV_PIC_PARAM_S receive = { .s32RecvPicNum = 1 };
        int ret = CVI_VENC_StartRecvFrame(j->channel[p], &receive);
        if (ret) { status = fail(error, "CVI_VENC_StartRecvFrame", ret); break; }
        j->receiving[p] = 1;
        /* VI owns the original YUV420 physical planes. No map/copy/VPSS here. */
        ret = CVI_VENC_SendFrame(j->channel[p], &input[p], 500);
        if (ret) { status = fail(error, "CVI_VENC_SendFrame", ret); break; }
        memset(&j->stream[p], 0, sizeof(j->stream[p]));
        j->stream[p].pstPack = &j->pack[p];
        j->stream[p].u32PackCount = 1;
        ret = CVI_VENC_GetStream(j->channel[p], &j->stream[p], 500);
        if (ret) { status = fail(error, "CVI_VENC_GetStream", ret); break; }
        j->held[p] = 1;
        VENC_PACK_S *pack = &j->pack[p];
        if (j->stream[p].u32PackCount != 1 || !pack->pu8Addr ||
            pack->u32Offset > pack->u32Len || pack->u32Len - pack->u32Offset < 4) {
            status = fail(error, "Invalid contiguous JPEG output", -1); break;
        }
        const uint8_t *data = pack->pu8Addr + pack->u32Offset;
        uint32_t size = pack->u32Len - pack->u32Offset;
        if (data[0] != 0xff || data[1] != 0xd8 || data[size-2] != 0xff || data[size-1] != 0xd9) {
            status = fail(error, "Incomplete JPEG output", -1); break;
        }
        pair->frames[p] = (tide_cam_jpeg_frame){data, size, (uint32_t)p,
            input[p].stVFrame.u32Width, input[p].stVFrame.u32Height,
            input[p].stVFrame.u64PTS * 1000};
    }
    if (status) {
        char cleanup[TIDE_CAM_ERROR_SIZE] = "";
        /* StopRecvFrame only changes channel state in this SDK. Destroy the
         * encoder context on faults before the owner may recycle VI input. */
        if (tide_jpeg_abort(j, cleanup)) fprintf(stderr, "tide_cam: %s\n", cleanup);
    }
    return status;
}
