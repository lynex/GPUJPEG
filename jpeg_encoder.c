/**
 * Copyright (c) 2011, Martin Srom
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
 
#include "jpeg_encoder.h"
#include "jpeg_preprocessor.h"
#include "jpeg_util.h"

/** Documented at declaration */
struct jpeg_encoder*
jpeg_encoder_create(int width, int height, int quality)
{
    struct jpeg_encoder* encoder = malloc(sizeof(struct jpeg_encoder));
    if ( encoder == NULL )
        return NULL;
    encoder->width = width;
    encoder->height = height;
    encoder->comp_count = 3;
    
    // Create tables
    encoder->table[JPEG_TABLE_LUMINANCE] = jpeg_table_create(JPEG_TABLE_LUMINANCE, quality);
    if ( encoder->table[JPEG_TABLE_LUMINANCE] == NULL )
        return NULL;
    encoder->table[JPEG_TABLE_CHROMINANCE] = jpeg_table_create(JPEG_TABLE_CHROMINANCE, quality);
    if ( encoder->table[JPEG_TABLE_CHROMINANCE] == NULL )
        return NULL;
    
    // Allocate data buffers
    int data_size = encoder->width * encoder->width * encoder->comp_count;
    uint8_t* d_image = NULL;
    if ( cudaSuccess != cudaMalloc((void**)&encoder->d_data_source, data_size * sizeof(uint8_t)) ) 
        return NULL;
    if ( cudaSuccess != cudaMalloc((void**)&encoder->d_data, data_size * sizeof(uint8_t)) ) 
        return NULL;
    if ( cudaSuccess != cudaMalloc((void**)&encoder->d_data_quantized, data_size * sizeof(int16_t)) ) 
        return NULL;
    
    return encoder;
}

void
jpeg_encoder_print8(struct jpeg_encoder* encoder, uint8_t* d_data)
{
    int data_size = encoder->width * encoder->height;
    uint8_t* data = NULL;
    cudaMallocHost((void**)&data, data_size * sizeof(uint8_t)); 
    cudaMemcpy(data, d_data, data_size * sizeof(uint8_t), cudaMemcpyDeviceToHost);
    
    printf("Print Data\n");
    for ( int y = 0; y < encoder->height; y++ ) {
        for ( int x = 0; x < encoder->width; x++ ) {
            printf("%4u ", data[y * encoder->width + x]);
        }
        printf("\n");
    }
    cudaFreeHost(data);
}

void
jpeg_encoder_print16(struct jpeg_encoder* encoder, int16_t* d_data)
{
    int data_size = encoder->width * encoder->height;
    int16_t* data = NULL;
    cudaMallocHost((void**)&data, data_size * sizeof(int16_t)); 
    cudaMemcpy(data, d_data, data_size * sizeof(int16_t), cudaMemcpyDeviceToHost);
    
    printf("Print Data\n");
    for ( int y = 0; y < encoder->height; y++ ) {
        for ( int x = 0; x < encoder->width; x++ ) {
            printf("%4d ", data[y * encoder->width + x]);
        }
        printf("\n");
    }
    cudaFreeHost(data);
}

/** Documented at declaration */
int
jpeg_encoder_encode(struct jpeg_encoder* encoder, uint8_t* image)
{
    //jpeg_table_print(encoder->table[JPEG_TABLE_LUMINANCE]);
    //jpeg_table_print(encoder->table[JPEG_TABLE_CHROMINANCE]);
    
    // Preprocessing
    if ( jpeg_preprocessor_process(encoder, image) != 0 )
        return -1;
        
    for ( int comp = 0; comp < encoder->comp_count; comp++ ) {
        uint8_t* d_data_comp = &encoder->d_data[comp * encoder->width * encoder->height];
        int16_t* d_data_quantized_comp = &encoder->d_data_quantized[comp * encoder->width * encoder->height];
        
        // Determine table type
        enum jpeg_table_type type = (comp == 0) ? JPEG_TABLE_LUMINANCE : JPEG_TABLE_CHROMINANCE;
        
        jpeg_encoder_print8(encoder, d_data_comp);
        
        cudaMemset(d_data_quantized_comp, 0, encoder->width * encoder->height * sizeof(int16_t));
        
        //Perform forward DCT
        NppiSize fwd_roi;
        fwd_roi.width = encoder->width;
        fwd_roi.height = encoder->height;
        NppStatus status = nppiDCTQuantFwd8x8LS_JPEG_8u16s_C1R(
            d_data_comp, 
            encoder->width * sizeof(uint8_t), 
            d_data_quantized_comp, 
            64 * sizeof(int16_t), 
            encoder->table[type]->d_table_forward, 
            fwd_roi
        );
        
        jpeg_encoder_print16(encoder, d_data_quantized_comp);

        cudaMemset(d_data_comp, 0, encoder->width * encoder->height * sizeof(int16_t));
        
        //Perform inverse DCT
        NppiSize inv_roi;
        inv_roi.width = 64;
        inv_roi.height = 1;
        nppiDCTQuantInv8x8LS_JPEG_16s8u_C1R(
            d_data_quantized_comp, 
            65 * sizeof(int16_t), 
            d_data_comp, 
            encoder->width * sizeof(uint8_t), 
            encoder->table[type]->d_table_inverse, 
            inv_roi
        );
        
        jpeg_encoder_print8(encoder, d_data_comp);
        
        break;
    }
    return 0;
}

/** Documented at declaration */
int
jpeg_encoder_destroy(struct jpeg_encoder* encoder)
{
    assert(encoder != NULL);
    
    assert(encoder->table[JPEG_TABLE_LUMINANCE] != NULL);
    jpeg_table_destroy(encoder->table[JPEG_TABLE_LUMINANCE]);
    assert(encoder->table[JPEG_TABLE_CHROMINANCE] != NULL);
    jpeg_table_destroy(encoder->table[JPEG_TABLE_CHROMINANCE]);
    
    assert(encoder->d_data_source != NULL);
    cudaFree(encoder->d_data_source);
    assert(encoder->d_data != NULL);
    cudaFree(encoder->d_data);
    assert(encoder->d_data_quantized != NULL);
    cudaFree(encoder->d_data_quantized);    
    
    free(encoder);
    
    return 0;
}
