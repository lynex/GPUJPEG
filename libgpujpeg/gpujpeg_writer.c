/**
 * Copyright (c) 2011, CESNET z.s.p.o
 * Copyright (c) 2011, Silicon Genome, LLC.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
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
 
#include "gpujpeg_writer.h"
#include "gpujpeg_format_type.h"
#include "gpujpeg_encoder.h"
#include "gpujpeg_util.h"

/** Documented at declaration */
struct gpujpeg_writer*
gpujpeg_writer_create(struct gpujpeg_encoder* encoder)
{
    struct gpujpeg_writer* writer = malloc(sizeof(struct gpujpeg_writer));
    if ( writer == NULL )
        return NULL;
    
    // Allocate output buffer
    int buffer_size = 1000;
    buffer_size += encoder->coder.param_image.width * encoder->coder.param_image.height * encoder->coder.param_image.comp_count * 2;
    writer->buffer = malloc(buffer_size * sizeof(uint8_t));
    if ( writer->buffer == NULL )
        return NULL;
    
    return writer;
}

/** Documented at declaration */
int
gpujpeg_writer_destroy(struct gpujpeg_writer* writer)
{
    assert(writer != NULL);
    assert(writer->buffer != NULL);
    free(writer->buffer);
    free(writer);
    return 0;
}

/**
 * Write SOI
 * 
 * @param writer  Writer structure
 * @return void
 */
void
gpujpeg_writer_write_soi(struct gpujpeg_writer* writer)
{
	gpujpeg_writer_emit_marker(writer, GPUJPEG_MARKER_SOI);
}

/**
 * Write APP0 block
 * 
 * @param writer  Writer structure
 * @return void
 */
void gpujpeg_writer_write_app0(struct gpujpeg_writer* writer)
{
    // Length of APP0 block	(2 bytes)
    // Block ID			(4 bytes - ASCII "JFIF")
    // Zero byte			(1 byte to terminate the ID string)
    // Version Major, Minor	(2 bytes - 0x01, 0x01)
    // Units			(1 byte - 0x00 = none, 0x01 = inch, 0x02 = cm)
    // Xdpu			(2 bytes - dots per unit horizontal)
    // Ydpu			(2 bytes - dots per unit vertical)
    // Thumbnail X size		(1 byte)
    // Thumbnail Y size		(1 byte)
    gpujpeg_writer_emit_marker(writer, GPUJPEG_MARKER_APP0);
    
    // Length
    gpujpeg_writer_emit_2byte(writer, 2 + 4 + 1 + 2 + 1 + 2 + 2 + 1 + 1);

    // Identifier: ASCII "JFIF"
    gpujpeg_writer_emit_byte(writer, 0x4A);
    gpujpeg_writer_emit_byte(writer, 0x46);
    gpujpeg_writer_emit_byte(writer, 0x49);
    gpujpeg_writer_emit_byte(writer, 0x46);
    gpujpeg_writer_emit_byte(writer, 0);

    // We currently emit version code 1.01 since we use no 1.02 features.
    // This may avoid complaints from some older decoders.
    // Major version
    gpujpeg_writer_emit_byte(writer, 1);
    // Minor version 
    gpujpeg_writer_emit_byte(writer, 1);
    // Pixel size information
    gpujpeg_writer_emit_byte(writer, 1);
    gpujpeg_writer_emit_2byte(writer, 300);
    gpujpeg_writer_emit_2byte(writer, 300);
    // No thumbnail image
    gpujpeg_writer_emit_byte(writer, 0);
    gpujpeg_writer_emit_byte(writer, 0);
}

/**
 * Write DQT block
 * 
 * @param encoder  Encoder structure
 * @param type  Component type for table retrieve
 * @return void
 */
void
gpujpeg_writer_write_dqt(struct gpujpeg_encoder* encoder, enum gpujpeg_component_type type)
{
	gpujpeg_writer_emit_marker(encoder->writer, GPUJPEG_MARKER_DQT);
    
    // Length
	gpujpeg_writer_emit_2byte(encoder->writer, 67);
    
    // Index: Y component = 0, Cb or Cr component = 1
	gpujpeg_writer_emit_byte(encoder->writer, (int)type); 

    // Table changed from default with quality
	uint8_t* dqt = encoder->table_quantization[type].table_raw;
    
    // Emit table
	unsigned char qval;
	for ( int i = 0; i < 64; i++ )  {
        unsigned char qval = (unsigned char)((char)(dqt[gpujpeg_order_natural[i]]));
		gpujpeg_writer_emit_byte(encoder->writer, qval);
    }
}

/**
 * Currently support GPUJPEG_MARKER_SOF0 baseline implementation
 * 
 * @param encoder  Encoder structure 
 * @return void
 */
void
gpujpeg_writer_write_sof0(struct gpujpeg_encoder* encoder)
{
	gpujpeg_writer_emit_marker(encoder->writer, GPUJPEG_MARKER_SOF0);
    
    // Length
	gpujpeg_writer_emit_2byte(encoder->writer, 8 + 3 * encoder->coder.param_image.comp_count);

    // Precision (bit depth)
	gpujpeg_writer_emit_byte(encoder->writer, 8);
    // Dimensions
	gpujpeg_writer_emit_2byte(encoder->writer, encoder->coder.param_image.height);
	gpujpeg_writer_emit_2byte(encoder->writer, encoder->coder.param_image.width);
    
    // Number of components
	gpujpeg_writer_emit_byte(encoder->writer, encoder->coder.param_image.comp_count);
    
    // Components
    for ( int comp_index = 0; comp_index < encoder->coder.param_image.comp_count; comp_index++ ) {
        // Get component
        struct gpujpeg_component* component = &encoder->coder.component[comp_index];
        
        // Component index
        gpujpeg_writer_emit_byte(encoder->writer, comp_index + 1);  
        
        // Sampling factors (1 << 4) + 1 (sampling h: 1, v: 1)
        gpujpeg_writer_emit_byte(encoder->writer, (component->sampling_factor.horizontal << 4) + component->sampling_factor.vertical);
        
        // Quantization table index
        if ( component->type == GPUJPEG_COMPONENT_LUMINANCE ) {
            gpujpeg_writer_emit_byte(encoder->writer, 0);
        } else if ( component->type == GPUJPEG_COMPONENT_CHROMINANCE ) {
            gpujpeg_writer_emit_byte(encoder->writer, 1);
        } else {
            assert(0);
        }
    }
}

/**
 * Write DHT block
 * 
 * @param encoder  Encoder structure
 * @param type  Component type for table retrieve
 * @param is_ac  Flag if table AC or DC should be written
 * @return void
 */
void
gpujpeg_writer_write_dht(struct gpujpeg_encoder* encoder, enum gpujpeg_component_type comp_type, enum gpujpeg_huffman_type huff_type)
{
    // Get proper table and its index
    struct gpujpeg_table_huffman_encoder* table = NULL;
	int index;
	if ( comp_type == GPUJPEG_COMPONENT_LUMINANCE ) {
		if ( huff_type == GPUJPEG_HUFFMAN_AC ) {
			table = &encoder->table_huffman[comp_type][huff_type];
			index = 16;
		} else {
			table = &encoder->table_huffman[comp_type][huff_type];
			index = 0;
		}
	} else {
		if ( huff_type == GPUJPEG_HUFFMAN_AC ) {
			table = &encoder->table_huffman[comp_type][huff_type];
			index = 17;
		} else {
			table = &encoder->table_huffman[comp_type][huff_type];
			index = 1;
		}
	}

	gpujpeg_writer_emit_marker(encoder->writer, GPUJPEG_MARKER_DHT);

	int length = 0;
    for ( int i = 1; i <= 16; i++ )
		length += table->bits[i];

	gpujpeg_writer_emit_2byte(encoder->writer, length + 2 + 1 + 16);

    gpujpeg_writer_emit_byte(encoder->writer, index);

	for ( int i = 1; i <= 16; i++ )
		gpujpeg_writer_emit_byte(encoder->writer, table->bits[i]);
    
    // Varible-length
    for ( int i = 0; i < length; i++ )
		gpujpeg_writer_emit_byte(encoder->writer, table->huffval[i]);  
}

/**
 * Write restart interval
 * 
 * @param encoder  Encoder structure
 * @return void
 */
void
gpujpeg_writer_write_dri(struct gpujpeg_encoder* encoder)
{
	gpujpeg_writer_emit_marker(encoder->writer, GPUJPEG_MARKER_DRI);
    
    // Length
	gpujpeg_writer_emit_2byte(encoder->writer, 4);

    // Restart interval
    gpujpeg_writer_emit_2byte(encoder->writer, encoder->coder.param.restart_interval);
}

/** Documented at declaration */
void
gpujpeg_writer_write_header(struct gpujpeg_encoder* encoder)
{        
	gpujpeg_writer_write_soi(encoder->writer);
	gpujpeg_writer_write_app0(encoder->writer);
    
	gpujpeg_writer_write_dqt(encoder, GPUJPEG_COMPONENT_LUMINANCE);      
	gpujpeg_writer_write_dqt(encoder, GPUJPEG_COMPONENT_CHROMINANCE);
	
    gpujpeg_writer_write_sof0(encoder);
    
	gpujpeg_writer_write_dht(encoder, GPUJPEG_COMPONENT_LUMINANCE, GPUJPEG_HUFFMAN_DC);   // DC table for Y component
	gpujpeg_writer_write_dht(encoder, GPUJPEG_COMPONENT_LUMINANCE, GPUJPEG_HUFFMAN_AC);   // AC table for Y component
	gpujpeg_writer_write_dht(encoder, GPUJPEG_COMPONENT_CHROMINANCE, GPUJPEG_HUFFMAN_DC); // DC table for Cb or Cr component
	gpujpeg_writer_write_dht(encoder, GPUJPEG_COMPONENT_CHROMINANCE, GPUJPEG_HUFFMAN_AC); // AC table for Cb or Cr component
    
    gpujpeg_writer_write_dri(encoder);
}

/** Documented at declaration */
void
gpujpeg_writer_write_scan_header(struct gpujpeg_encoder* encoder, int scan_index)
{        
    gpujpeg_writer_emit_marker(encoder->writer, GPUJPEG_MARKER_SOS);
    
    if ( encoder->coder.param.interleaved == 1 ) {
        // Length
        gpujpeg_writer_emit_2byte(encoder->writer, 6 + 2 * encoder->coder.param_image.comp_count);
        
        // Component count
        gpujpeg_writer_emit_byte(encoder->writer, encoder->coder.param_image.comp_count);
        
        // Components
        for ( int comp_index = 0; comp_index < encoder->coder.param_image.comp_count; comp_index++ ) {
            // Get component
            struct gpujpeg_component* component = &encoder->coder.component[comp_index];
        
            // Component index
            gpujpeg_writer_emit_byte(encoder->writer, comp_index + 1);
            
            // Component DC and AC entropy coding table indexes
            if ( component->type == GPUJPEG_COMPONENT_LUMINANCE ) {
                gpujpeg_writer_emit_byte(encoder->writer, 0);    // (0 << 4) | 0
            } else if ( component->type == GPUJPEG_COMPONENT_CHROMINANCE ) {
                gpujpeg_writer_emit_byte(encoder->writer, 0x11); // (1 << 4) | 1
            } else {
                assert(0);
            }
        }
    } else {
        // Component index
        int comp_index = scan_index;
        
        // Get component
        struct gpujpeg_component* component = &encoder->coder.component[comp_index];

        // Length
        gpujpeg_writer_emit_2byte(encoder->writer, 8);

        // Component count
        gpujpeg_writer_emit_byte(encoder->writer, 1);

        // Component index
        gpujpeg_writer_emit_byte(encoder->writer, comp_index + 1);
        
        // Component DC and AC entropy coding table indexes
        if ( component->type == GPUJPEG_COMPONENT_LUMINANCE ) {
            gpujpeg_writer_emit_byte(encoder->writer, 0);    // (0 << 4) | 0
        } else if ( component->type == GPUJPEG_COMPONENT_CHROMINANCE ) {
            gpujpeg_writer_emit_byte(encoder->writer, 0x11); // (1 << 4) | 1
        } else {
            assert(0);
        }
    }
    
    gpujpeg_writer_emit_byte(encoder->writer, 0);    // Ss
    gpujpeg_writer_emit_byte(encoder->writer, 0x3F); // Se
    gpujpeg_writer_emit_byte(encoder->writer, 0);    // Ah/Al
}
