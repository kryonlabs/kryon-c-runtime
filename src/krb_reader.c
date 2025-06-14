#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include "krb.h"

// --- Helper Functions ---

// Read little-endian uint16_t
uint16_t krb_read_u16_le(const void* data) {
    if (!data) return 0;
    const unsigned char* p = (const unsigned char*)data;
    return (uint16_t)(p[0] | (p[1] << 8));
}

// Read little-endian uint32_t
uint32_t krb_read_u32_le(const void* data) {
    if (!data) return 0;
    const unsigned char* p = (const unsigned char*)data;
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

// --- Internal Read Helpers ---

// Reads the main file header (KRB v0.4 - 48 bytes)
static bool read_header_internal(FILE* file, KrbHeader* header) {
    unsigned char buffer[48]; // Updated size to 48 bytes
    if (!file || !header) return false;
    if (fseek(file, 0, SEEK_SET) != 0) {
        perror("Error seeking to start of file"); 
        return false;
    }
    size_t bytes_read = fread(buffer, 1, sizeof(buffer), file);
    if (bytes_read < sizeof(buffer)) {
        fprintf(stderr, "Error: Failed to read %zu-byte header, got %zu bytes\n", sizeof(buffer), bytes_read);
        return false;
    }

    // Parse header fields using helpers for endianness
    memcpy(header->magic, buffer + 0, 4);
    header->version              = krb_read_u16_le(buffer + 4);
    header->flags                = krb_read_u16_le(buffer + 6);
    header->element_count        = krb_read_u16_le(buffer + 8);
    header->style_count          = krb_read_u16_le(buffer + 10);
    header->component_def_count  = krb_read_u16_le(buffer + 12);  // NEW
    header->animation_count      = krb_read_u16_le(buffer + 14);
    header->string_count         = krb_read_u16_le(buffer + 16);
    header->resource_count       = krb_read_u16_le(buffer + 18);
    header->element_offset       = krb_read_u32_le(buffer + 20);
    header->style_offset         = krb_read_u32_le(buffer + 24);
    header->component_def_offset = krb_read_u32_le(buffer + 28);  // NEW
    header->animation_offset     = krb_read_u32_le(buffer + 32);
    header->string_offset        = krb_read_u32_le(buffer + 36);
    header->resource_offset      = krb_read_u32_le(buffer + 40);
    header->total_size           = krb_read_u32_le(buffer + 44);

    // Basic Validations
    if (memcmp(header->magic, "KRB1", 4) != 0) {
        fprintf(stderr, "Error: Invalid magic number. Expected 'KRB1', got '%.4s'\n", header->magic);
        return false;
    }
    
    uint8_t major = (header->version & 0x00FF);
    uint8_t minor = (header->version >> 8);
    if (major != KRB_SPEC_VERSION_MAJOR || minor != KRB_SPEC_VERSION_MINOR) {
        fprintf(stderr, "Warning: KRB version mismatch. Expected %d.%d, got %d.%d. Parsing continues but may be unreliable.\n",
                KRB_SPEC_VERSION_MAJOR, KRB_SPEC_VERSION_MINOR, major, minor);
    }
    
    // Check reasonable offset values
    uint32_t min_offset = sizeof(KrbHeader);
    if (header->element_count > 0 && header->element_offset < min_offset) { 
        fprintf(stderr, "Error: Element offset (%u) overlaps header (%u).\n", header->element_offset, min_offset); 
        return false; 
    }
    if (header->style_count > 0 && header->style_offset < min_offset) { 
        fprintf(stderr, "Error: Style offset (%u) overlaps header.\n", header->style_offset); 
        return false; 
    }
    if (header->component_def_count > 0 && header->component_def_offset < min_offset) { 
        fprintf(stderr, "Error: Component def offset (%u) overlaps header.\n", header->component_def_offset); 
        return false; 
    }
    if (header->resource_count > 0 && header->resource_offset < min_offset) { 
        fprintf(stderr, "Error: Resource offset (%u) overlaps header.\n", header->resource_offset); 
        return false; 
    }

    return true;
}

// Reads element header (17 bytes for v0.4)
static bool read_element_header_internal(FILE* file, KrbElementHeader* element) {
    if (!file || !element) {
        fprintf(stderr, "Error: NULL file or element pointer passed to read_element_header_internal.\n");
        return false;
    }

    size_t expected_size = sizeof(KrbElementHeader);
    if (expected_size != 17) {
        fprintf(stderr, "Error: KrbElementHeader size mismatch! Expected 17, got %zu. Check krb.h definition and packing.\n", expected_size);
        return false;
    }

    size_t bytes_read = fread(element, 1, expected_size, file);
    if (bytes_read != expected_size) {
        fprintf(stderr, "Error: Failed to read %zu bytes for element header, got %zu.\n", expected_size, bytes_read);
        if (feof(file)) { fprintf(stderr, "  (End of file reached prematurely)\n"); }
        else if (ferror(file)) { perror("  (File read error)"); }
        return false;
    }

    // Correct endianness for multi-byte fields
    element->pos_x = krb_read_u16_le(&element->pos_x);
    element->pos_y = krb_read_u16_le(&element->pos_y);
    element->width = krb_read_u16_le(&element->width);
    element->height = krb_read_u16_le(&element->height);

    return true;
}

// Reads a single standard property
static bool read_property_internal(FILE* file, KrbProperty* prop) {
    unsigned char buffer[3]; // ID(1)+Type(1)+Size(1)
    long prop_header_offset = ftell(file);
    if (fread(buffer, 1, 3, file) != 3) {
        fprintf(stderr, "Error: Failed reading property header @ %ld\n", prop_header_offset);
        prop->value = NULL; 
        prop->size = 0; 
        return false;
    }
    prop->property_id = buffer[0]; 
    prop->value_type = buffer[1]; 
    prop->size = buffer[2];
    prop->value = NULL;
    
    if (prop->size > 0) {
        prop->value = malloc(prop->size);
        if (!prop->value) { 
            perror("malloc prop value"); 
            return false; 
        }
        if (fread(prop->value, 1, prop->size, file) != prop->size) {
            fprintf(stderr, "Error: Failed reading %u bytes prop value (ID 0x%02X) @ %ld\n", 
                    prop->size, prop->property_id, ftell(file) - prop->size);
            free(prop->value); 
            prop->value = NULL; 
            return false;
        }
    }
    return true;
}

// NEW: Reads a single custom property
static bool read_custom_property_internal(FILE* file, KrbCustomProperty* custom_prop) {
    unsigned char buffer[3]; // KeyIndex(1)+Type(1)+Size(1)
    long prop_header_offset = ftell(file);
    if (fread(buffer, 1, 3, file) != 3) {
        fprintf(stderr, "Error: Failed reading custom property header @ %ld\n", prop_header_offset);
        custom_prop->value = NULL; 
        custom_prop->value_size = 0; 
        return false;
    }
    custom_prop->key_index = buffer[0]; 
    custom_prop->value_type = buffer[1]; 
    custom_prop->value_size = buffer[2];
    custom_prop->value = NULL;
    
    if (custom_prop->value_size > 0) {
        custom_prop->value = malloc(custom_prop->value_size);
        if (!custom_prop->value) { 
            perror("malloc custom prop value"); 
            return false; 
        }
        if (fread(custom_prop->value, 1, custom_prop->value_size, file) != custom_prop->value_size) {
            fprintf(stderr, "Error: Failed reading %u bytes custom prop value (KeyIdx %u) @ %ld\n", 
                    custom_prop->value_size, custom_prop->key_index, ftell(file) - custom_prop->value_size);
            free(custom_prop->value); 
            custom_prop->value = NULL; 
            return false;
        }
    }
    return true;
}

// --- Public API Functions ---

// Reads the entire KRB document structure into memory.
bool krb_read_document(FILE* file, KrbDocument* doc) {
    if (!file || !doc) return false;
    memset(doc, 0, sizeof(KrbDocument));

    // Read and validate header
    if (!read_header_internal(file, &doc->header)) {
        return false;
    }
    // Store parsed version components
    doc->version_major = (doc->header.version & 0x00FF);
    doc->version_minor = (doc->header.version >> 8);

    // Validate App element presence if flag is set
    if ((doc->header.flags & FLAG_HAS_APP) && doc->header.element_count > 0) {
        long original_pos = ftell(file);
        if (fseek(file, doc->header.element_offset, SEEK_SET) != 0) { 
            perror("seek App check"); 
            krb_free_document(doc); 
            return false; 
        }
        unsigned char first_type;
        if (fread(&first_type, 1, 1, file) != 1) { 
            fprintf(stderr, "read App check failed\n"); 
            fseek(file, original_pos, SEEK_SET); 
            krb_free_document(doc); 
            return false; 
        }
        if (first_type != ELEM_TYPE_APP) { 
            fprintf(stderr, "Error: FLAG_HAS_APP set, but first elem type 0x%02X != 0x00\n", first_type); 
            fseek(file, original_pos, SEEK_SET); 
            krb_free_document(doc); 
            return false; 
        }
        if (fseek(file, original_pos, SEEK_SET) != 0) { 
            perror("seek back App check"); 
            krb_free_document(doc); 
            return false; 
        }
    }

    // --- Read Elements, Properties, Custom Properties, and Events ---
    if (doc->header.element_count > 0) {
        if (doc->header.element_offset == 0) { 
            fprintf(stderr, "Error: Zero element offset with non-zero count.\n"); 
            krb_free_document(doc); 
            return false; 
        }

        // Allocate memory
        doc->elements = calloc(doc->header.element_count, sizeof(KrbElementHeader));
        doc->properties = calloc(doc->header.element_count, sizeof(KrbProperty*));
        doc->custom_properties = calloc(doc->header.element_count, sizeof(KrbCustomProperty*)); // NEW
        doc->events = calloc(doc->header.element_count, sizeof(KrbEventFileEntry*));
        
        if (!doc->elements || !doc->properties || !doc->custom_properties || !doc->events) {
            perror("calloc elements/props/custom_props/events ptrs"); 
            krb_free_document(doc); 
            return false;
        }

        if (fseek(file, doc->header.element_offset, SEEK_SET) != 0) {
            perror("seek element data"); 
            krb_free_document(doc); 
            return false;
        }

        for (uint16_t i = 0; i < doc->header.element_count; i++) {
            // Read element header
            if (!read_element_header_internal(file, &doc->elements[i])) {
                fprintf(stderr, "Failed reading header elem %u\n", i); 
                krb_free_document(doc); 
                return false;
            }

            // Read standard properties
            if (doc->elements[i].property_count > 0) {
                doc->properties[i] = calloc(doc->elements[i].property_count, sizeof(KrbProperty));
                if (!doc->properties[i]) { 
                    perror("calloc props elem"); 
                    fprintf(stderr, "Elem %u\n", i); 
                    krb_free_document(doc); 
                    return false; 
                }
                for (uint8_t j = 0; j < doc->elements[i].property_count; j++) {
                    if (!read_property_internal(file, &doc->properties[i][j])) {
                        fprintf(stderr, "Failed reading prop %u elem %u\n", j, i); 
                        krb_free_document(doc); 
                        return false;
                    }
                }
            }

            // NEW: Read custom properties
            if (doc->elements[i].custom_prop_count > 0) {
                doc->custom_properties[i] = calloc(doc->elements[i].custom_prop_count, sizeof(KrbCustomProperty));
                if (!doc->custom_properties[i]) { 
                    perror("calloc custom props elem"); 
                    fprintf(stderr, "Elem %u\n", i); 
                    krb_free_document(doc); 
                    return false; 
                }
                for (uint8_t j = 0; j < doc->elements[i].custom_prop_count; j++) {
                    if (!read_custom_property_internal(file, &doc->custom_properties[i][j])) {
                        fprintf(stderr, "Failed reading custom prop %u elem %u\n", j, i); 
                        krb_free_document(doc); 
                        return false;
                    }
                }
            }

            // Read Events
            if (doc->elements[i].event_count > 0) {
                doc->events[i] = calloc(doc->elements[i].event_count, sizeof(KrbEventFileEntry));
                if (!doc->events[i]) { 
                    perror("calloc events elem"); 
                    fprintf(stderr, "Elem %u\n", i); 
                    krb_free_document(doc); 
                    return false; 
                }
                size_t events_to_read = doc->elements[i].event_count;
                size_t events_read = fread(doc->events[i], sizeof(KrbEventFileEntry), events_to_read, file);
                if (events_read != events_to_read) {
                    fprintf(stderr, "Error: Read %zu/%zu events elem %u\n", events_read, events_to_read, i); 
                    krb_free_document(doc); 
                    return false;
                }
            }

            // Skip Animation Refs and Child Refs
            long bytes_to_skip = (long)doc->elements[i].animation_count * 2 // Anim Index(1)+Trigger(1)
                               + (long)doc->elements[i].child_count * 2;   // Child Offset(2)
            if (bytes_to_skip > 0) {
                if (fseek(file, bytes_to_skip, SEEK_CUR) != 0) {
                    perror("seek skip refs"); 
                    fprintf(stderr, "Elem %u\n", i); 
                    krb_free_document(doc); 
                    return false;
                }
            }
        }
    }

    // --- Read Styles ---
    if (doc->header.style_count > 0) {
        if (doc->header.style_offset == 0) { 
            fprintf(stderr, "Error: Zero style offset with non-zero count.\n"); 
            krb_free_document(doc); 
            return false; 
        }
        doc->styles = calloc(doc->header.style_count, sizeof(KrbStyle));
        if (!doc->styles) { 
            perror("calloc styles"); 
            krb_free_document(doc); 
            return false; 
        }
        if (fseek(file, doc->header.style_offset, SEEK_SET) != 0) { 
            perror("seek styles"); 
            krb_free_document(doc); 
            return false; 
        }

        for (uint16_t i = 0; i < doc->header.style_count; i++) {
            unsigned char style_header_buf[3]; // ID(1)+NameIdx(1)+PropCount(1)
            if (fread(style_header_buf, 1, 3, file) != 3) { 
                fprintf(stderr, "Failed read style header %u\n", i); 
                krb_free_document(doc); 
                return false; 
            }
            doc->styles[i].id = style_header_buf[0]; // 1-based ID
            doc->styles[i].name_index = style_header_buf[1]; // 0-based index
            doc->styles[i].property_count = style_header_buf[2];
            doc->styles[i].properties = NULL;

            if (doc->styles[i].property_count > 0) {
                doc->styles[i].properties = calloc(doc->styles[i].property_count, sizeof(KrbProperty));
                if (!doc->styles[i].properties) { 
                    perror("calloc style props"); 
                    fprintf(stderr, "Style %u\n", i); 
                    krb_free_document(doc); 
                    return false; 
                }
                for (uint8_t j = 0; j < doc->styles[i].property_count; j++) {
                    if (!read_property_internal(file, &doc->styles[i].properties[j])) { 
                        fprintf(stderr, "Failed read prop %u style %u\n", j, i); 
                        krb_free_document(doc); 
                        return false; 
                    }
                }
            }
        }
    }

    // --- NEW: Read Component Definitions ---
    if (doc->header.component_def_count > 0) {
        if (doc->header.component_def_offset == 0) { 
            fprintf(stderr, "Error: Zero component def offset with non-zero count.\n"); 
            krb_free_document(doc); 
            return false; 
        }
        doc->component_defs = calloc(doc->header.component_def_count, sizeof(KrbComponentDefinition));
        if (!doc->component_defs) { 
            perror("calloc component defs"); 
            krb_free_document(doc); 
            return false; 
        }
        if (fseek(file, doc->header.component_def_offset, SEEK_SET) != 0) { 
            perror("seek component defs"); 
            krb_free_document(doc); 
            return false; 
        }

        for (uint16_t i = 0; i < doc->header.component_def_count; i++) {
            unsigned char comp_header_buf[2]; // NameIdx(1)+PropDefCount(1)
            if (fread(comp_header_buf, 1, 2, file) != 2) { 
                fprintf(stderr, "Failed read component def header %u\n", i); 
                krb_free_document(doc); 
                return false; 
            }
            doc->component_defs[i].name_index = comp_header_buf[0];
            doc->component_defs[i].property_def_count = comp_header_buf[1];
            doc->component_defs[i].property_defs = NULL;

            // Read property definitions
            if (doc->component_defs[i].property_def_count > 0) {
                doc->component_defs[i].property_defs = calloc(doc->component_defs[i].property_def_count, sizeof(KrbPropertyDefinition));
                if (!doc->component_defs[i].property_defs) { 
                    perror("calloc component prop defs"); 
                    fprintf(stderr, "Component %u\n", i); 
                    krb_free_document(doc); 
                    return false; 
                }
                
                for (uint8_t j = 0; j < doc->component_defs[i].property_def_count; j++) {
                    unsigned char prop_def_buf[3]; // NameIdx(1)+TypeHint(1)+DefaultSize(1)
                    if (fread(prop_def_buf, 1, 3, file) != 3) { 
                        fprintf(stderr, "Failed read prop def %u component %u\n", j, i); 
                        krb_free_document(doc); 
                        return false; 
                    }
                    doc->component_defs[i].property_defs[j].name_index = prop_def_buf[0];
                    doc->component_defs[i].property_defs[j].value_type_hint = prop_def_buf[1];
                    doc->component_defs[i].property_defs[j].default_value_size = prop_def_buf[2];
                    doc->component_defs[i].property_defs[j].default_value_data = NULL;

                    // Read default value if present
                    if (doc->component_defs[i].property_defs[j].default_value_size > 0) {
                        doc->component_defs[i].property_defs[j].default_value_data = malloc(doc->component_defs[i].property_defs[j].default_value_size);
                        if (!doc->component_defs[i].property_defs[j].default_value_data) { 
                            perror("malloc component prop def default value"); 
                            krb_free_document(doc); 
                            return false; 
                        }
                        if (fread(doc->component_defs[i].property_defs[j].default_value_data, 1, 
                                 doc->component_defs[i].property_defs[j].default_value_size, file) != 
                            doc->component_defs[i].property_defs[j].default_value_size) {
                            fprintf(stderr, "Failed read prop def default value %u component %u\n", j, i); 
                            krb_free_document(doc); 
                            return false;
                        }
                    }
                }
            }

            // Read root element template
            // For simplicity, we'll just read the header for now
            // Full implementation would need to handle the complete element block structure
            if (!read_element_header_internal(file, &doc->component_defs[i].root_template_header)) {
                fprintf(stderr, "Failed reading root template header component %u\n", i); 
                krb_free_document(doc); 
                return false;
            }
            
            // Initialize template property arrays to NULL (full parsing would follow)
            doc->component_defs[i].root_template_properties = NULL;
            doc->component_defs[i].root_template_custom_props = NULL;
            doc->component_defs[i].root_template_events = NULL;
            
            // Skip the rest of the template for now (properties, custom props, events, children)
            // This is a simplified implementation - full version would parse these
            long template_bytes_to_skip = 
                (long)doc->component_defs[i].root_template_header.property_count * 3 + // Property headers (minimum)
                (long)doc->component_defs[i].root_template_header.custom_prop_count * 3 + // Custom prop headers (minimum)
                (long)doc->component_defs[i].root_template_header.event_count * 2 + // Events
                (long)doc->component_defs[i].root_template_header.animation_count * 2 + // Animation refs
                (long)doc->component_defs[i].root_template_header.child_count * 2; // Child refs
            
            // This is an approximation - real implementation would need to parse each section properly
            // to know exact sizes including variable-length property values
            if (fseek(file, template_bytes_to_skip, SEEK_CUR) != 0) {
                fprintf(stderr, "Warning: Failed to skip template data for component %u\n", i);
            }
        }
    }

    // --- Read Animations (Skipped) ---
    // TODO: Implement animation reading if needed

    // --- Read Strings ---
    if (doc->header.string_count > 0) {
        if (doc->header.string_offset == 0) { 
            fprintf(stderr, "Error: Zero string offset with non-zero count.\n"); 
            krb_free_document(doc); 
            return false; 
        }
        doc->strings = calloc(doc->header.string_count, sizeof(char*));
        if (!doc->strings) { 
            perror("calloc strings ptrs"); 
            krb_free_document(doc); 
            return false; 
        }
        if (fseek(file, doc->header.string_offset, SEEK_SET) != 0) { 
            perror("seek strings"); 
            krb_free_document(doc); 
            return false; 
        }

        unsigned char stc_bytes[2]; 
        if (fread(stc_bytes, 1, 2, file) != 2) { 
            fprintf(stderr, "Failed read string table count\n"); 
            krb_free_document(doc); 
            return false; 
        }
        uint16_t table_count = krb_read_u16_le(stc_bytes);
        if (table_count != doc->header.string_count) { 
            fprintf(stderr, "Warning: Header string count %u != table count %u\n", doc->header.string_count, table_count); 
        }

        for (uint16_t i = 0; i < doc->header.string_count; i++) {
            uint8_t length;
            if (fread(&length, 1, 1, file) != 1) { 
                fprintf(stderr, "Failed read str len %u\n", i); 
                krb_free_document(doc); 
                return false; 
            }
            doc->strings[i] = malloc(length + 1);
            if (!doc->strings[i]) { 
                perror("malloc string"); 
                fprintf(stderr, "String %u\n", i); 
                krb_free_document(doc); 
                return false; 
            }
            if (length > 0) {
                if (fread(doc->strings[i], 1, length, file) != length) { 
                    fprintf(stderr, "Failed read %u bytes str %u\n", length, i); 
                    krb_free_document(doc); 
                    return false; 
                }
            }
            doc->strings[i][length] = '\0';
        }
    }

    // --- Read Resources ---
    if (doc->header.resource_count > 0) {
        if (doc->header.resource_offset == 0) { 
            fprintf(stderr, "Error: Zero resource offset with non-zero count.\n"); 
            krb_free_document(doc); 
            return false; 
        }
        doc->resources = calloc(doc->header.resource_count, sizeof(KrbResource));
        if (!doc->resources) { 
            perror("calloc resources"); 
            krb_free_document(doc); 
            return false; 
        }
        if (fseek(file, doc->header.resource_offset, SEEK_SET) != 0) { 
            perror("seek resources"); 
            krb_free_document(doc); 
            return false; 
        }

        unsigned char resc_bytes[2];
        if (fread(resc_bytes, 1, 2, file) != 2) { 
            fprintf(stderr, "Failed read resource table count\n"); 
            krb_free_document(doc); 
            return false; 
        }
        uint16_t table_res_count = krb_read_u16_le(resc_bytes);
        if (table_res_count != doc->header.resource_count) { 
            fprintf(stderr, "Warning: Header resource count %u != table count %u\n", doc->header.resource_count, table_res_count); 
        }

        for (uint16_t i = 0; i < doc->header.resource_count; i++) {
            unsigned char res_entry_buf[4]; // Type(1)+NameIdx(1)+Format(1)+DataIdx(1)
            if (fread(res_entry_buf, 1, 4, file) != 4) {
                fprintf(stderr, "Error: Failed read resource entry %u\n", i);
                krb_free_document(doc); 
                return false;
            }
            doc->resources[i].type = res_entry_buf[0];
            doc->resources[i].name_index = res_entry_buf[1];
            doc->resources[i].format = res_entry_buf[2];
            
            if (doc->resources[i].format == RES_FORMAT_EXTERNAL) {
                doc->resources[i].data_string_index = res_entry_buf[3];
            } else if (doc->resources[i].format == RES_FORMAT_INLINE) {
                fprintf(stderr, "Error: Inline resource parsing not yet implemented (Res %u).\n", i);
                krb_free_document(doc); 
                return false;
            } else {
                fprintf(stderr, "Error: Unknown resource format 0x%02X for resource %u\n", doc->resources[i].format, i);
                krb_free_document(doc); 
                return false;
            }
        }
    }

    return true;
}

// Frees all memory allocated within the KrbDocument structure.
void krb_free_document(KrbDocument* doc) {
    if (!doc) return;

    // Free Element Data (Properties, Custom Properties, and Events)
    if (doc->elements) {
        for (uint16_t i = 0; i < doc->header.element_count; i++) {
            // Free standard properties
            if (doc->properties && doc->properties[i]) {
                for (uint8_t j = 0; j < doc->elements[i].property_count; j++) {
                    if (doc->properties[i][j].value) {
                        free(doc->properties[i][j].value);
                    }
                }
                free(doc->properties[i]);
            }
            
            // NEW: Free custom properties
            if (doc->custom_properties && doc->custom_properties[i]) {
                for (uint8_t j = 0; j < doc->elements[i].custom_prop_count; j++) {
                    if (doc->custom_properties[i][j].value) {
                        free(doc->custom_properties[i][j].value);
                    }
                }
                free(doc->custom_properties[i]);
            }
            
            // Free events
            if (doc->events && doc->events[i]) {
                free(doc->events[i]);
            }
        }
    }
    
    // Free top-level pointer arrays
    if (doc->properties) free(doc->properties);
    if (doc->custom_properties) free(doc->custom_properties); // NEW
    if (doc->events) free(doc->events);
    if (doc->elements) free(doc->elements);

    // Free Styles
    if (doc->styles) {
        for (uint16_t i = 0; i < doc->header.style_count; i++) {
            if (doc->styles[i].properties) {
                for (uint8_t j = 0; j < doc->styles[i].property_count; j++) {
                    if (doc->styles[i].properties[j].value) {
                        free(doc->styles[i].properties[j].value);
                    }
                }
                free(doc->styles[i].properties);
            }
        }
        free(doc->styles);
    }

    // NEW: Free Component Definitions
    if (doc->component_defs) {
        for (uint16_t i = 0; i < doc->header.component_def_count; i++) {
            if (doc->component_defs[i].property_defs) {
                for (uint8_t j = 0; j < doc->component_defs[i].property_def_count; j++) {
                    if (doc->component_defs[i].property_defs[j].default_value_data) {
                        free(doc->component_defs[i].property_defs[j].default_value_data);
                    }
                }
                free(doc->component_defs[i].property_defs);
            }
            // Free template arrays (if they were allocated)
            if (doc->component_defs[i].root_template_properties) {
                // Would need to free individual property values here
                free(doc->component_defs[i].root_template_properties);
            }
            if (doc->component_defs[i].root_template_custom_props) {
                // Would need to free individual custom property values here
                free(doc->component_defs[i].root_template_custom_props);
            }
            if (doc->component_defs[i].root_template_events) {
                free(doc->component_defs[i].root_template_events);
            }
        }
        free(doc->component_defs);
    }

    // Free Strings
    if (doc->strings) {
        for (uint16_t i = 0; i < doc->header.string_count; i++) {
            if (doc->strings[i]) {
                free(doc->strings[i]);
            }
        }
        free(doc->strings);
    }

    // Free Resources
    if (doc->resources) {
        free(doc->resources);
    }
}