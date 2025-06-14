#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <math.h> 
#include <libgen.h> 

#include "renderer.h" 

// --- Basic Definitions ---
#define DEFAULT_WINDOW_WIDTH 800
#define DEFAULT_WINDOW_HEIGHT 600
#define DEFAULT_SCALE_FACTOR 1.0f
#define BASE_FONT_SIZE 20

// --- Component Instantiation Functions ---

bool find_component_name_property(KrbCustomProperty* custom_props, uint8_t custom_prop_count, 
                                 char** strings, uint8_t* out_component_index) {
    if (!custom_props || !strings || !out_component_index) return false;
    
    for (uint8_t i = 0; i < custom_prop_count; i++) {
        KrbCustomProperty* prop = &custom_props[i];
        
        // Check if this property key is "_componentName"
        if (prop->key_index < MAX_ELEMENTS && strings[prop->key_index] && 
            strcmp(strings[prop->key_index], "_componentName") == 0) {
            
            // Value should be a string index pointing to the component name
            if (prop->value_type == VAL_TYPE_STRING && prop->value_size == 1 && prop->value) {
                *out_component_index = *(uint8_t*)prop->value;
                return true;
            }
        }
    }
    return false;
}

void apply_property_to_element(RenderElement* element, KrbProperty* prop, KrbDocument* doc, FILE* debug_file) {
    if (!element || !prop || !prop->value) return;
    
    switch (prop->property_id) {
        case PROP_ID_BG_COLOR:
            if (prop->value_type == VAL_TYPE_COLOR && prop->size == 4) {
                uint8_t* c = (uint8_t*)prop->value;
                element->bg_color = (Color){c[0], c[1], c[2], c[3]};
            }
            break;
            
        case PROP_ID_FG_COLOR:
            if (prop->value_type == VAL_TYPE_COLOR && prop->size == 4) {
                uint8_t* c = (uint8_t*)prop->value;
                element->fg_color = (Color){c[0], c[1], c[2], c[3]};
            }
            break;
            
        case PROP_ID_BORDER_COLOR:
            if (prop->value_type == VAL_TYPE_COLOR && prop->size == 4) {
                uint8_t* c = (uint8_t*)prop->value;
                element->border_color = (Color){c[0], c[1], c[2], c[3]};
            }
            break;
            
        case PROP_ID_BORDER_WIDTH:
            if (prop->value_type == VAL_TYPE_BYTE && prop->size == 1) {
                memset(element->border_widths, *(uint8_t*)prop->value, 4);
            } else if (prop->value_type == VAL_TYPE_EDGEINSETS && prop->size == 4) {
                memcpy(element->border_widths, prop->value, 4);
            }
            break;
            
        case PROP_ID_TEXT_CONTENT:
            if (prop->value_type == VAL_TYPE_STRING && prop->size == 1) {
                uint8_t idx = *(uint8_t*)prop->value;
                if (idx < doc->header.string_count && doc->strings[idx]) {
                    free(element->text);
                    element->text = strdup(doc->strings[idx]);
                }
            }
            break;
            
        case PROP_ID_TEXT_ALIGNMENT:
            if (prop->value_type == VAL_TYPE_ENUM && prop->size == 1) {
                element->text_alignment = *(uint8_t*)prop->value;
            }
            break;
            
        case PROP_ID_IMAGE_SOURCE:
            if (prop->value_type == VAL_TYPE_RESOURCE && prop->size == 1) {
                element->resource_index = *(uint8_t*)prop->value;
            }
            break;
            
        default:
            // Unknown property, ignore
            break;
    }
}

RenderElement* create_element_from_template(RenderContext* ctx, KrbElementHeader* template_header,
                                          KrbProperty* template_properties, int template_prop_count,
                                          FILE* debug_file) {
    if (!ctx || !template_header) return NULL;
    
    // Allocate new render element
    RenderElement* new_element = calloc(1, sizeof(RenderElement));
    if (!new_element) {
        if (debug_file) fprintf(debug_file, "ERROR: Failed to allocate memory for template element\n");
        return NULL;
    }
    
    // Copy template header
    new_element->header = *template_header;
    new_element->original_index = -1; // Mark as instantiated
    new_element->is_component_instance = false;
    new_element->is_placeholder = false;
    new_element->component_instance = NULL;
    
    // Initialize default colors from context
    new_element->bg_color = ctx->default_bg;
    new_element->fg_color = ctx->default_fg;
    new_element->border_color = ctx->default_border;
    memset(new_element->border_widths, 0, 4);
    new_element->text_alignment = 0;
    new_element->texture_loaded = false;
    new_element->resource_index = INVALID_RESOURCE_INDEX;
    new_element->custom_properties = NULL;
    new_element->custom_prop_count = 0;
    new_element->parent = NULL;
    new_element->child_count = 0;
    new_element->is_interactive = (new_element->header.type == ELEM_TYPE_BUTTON || new_element->header.type == ELEM_TYPE_INPUT);
    
    for(int k = 0; k < MAX_ELEMENTS; ++k) new_element->children[k] = NULL;
    
    // Apply template properties
    for (int j = 0; j < template_prop_count; j++) {
        apply_property_to_element(new_element, &template_properties[j], ctx->doc, debug_file);
    }
    
    if (debug_file) {
        fprintf(debug_file, "  Created element from template: Type=0x%02X, Text='%s'\n", 
                new_element->header.type, new_element->text ? new_element->text : "NULL");
    }
    
    return new_element;
}

void apply_instance_properties(RenderElement* instance_root, RenderElement* placeholder, FILE* debug_file) {
    if (!instance_root || !placeholder) return;
    
    // Apply header properties from placeholder to instance root
    if (placeholder->header.pos_x != 0 || placeholder->header.pos_y != 0) {
        instance_root->header.pos_x = placeholder->header.pos_x;
        instance_root->header.pos_y = placeholder->header.pos_y;
    }
    
    if (placeholder->header.width != 0) {
        instance_root->header.width = placeholder->header.width;
    }
    
    if (placeholder->header.height != 0) {
        instance_root->header.height = placeholder->header.height;
    }
    
    // Apply style if specified
    if (placeholder->header.style_id != 0) {
        instance_root->header.style_id = placeholder->header.style_id;
    }
    
    // Apply layout if specified
    if (placeholder->header.layout != 0) {
        instance_root->header.layout = placeholder->header.layout;
    }
    
    // Copy ID for identification
    instance_root->header.id = placeholder->header.id;
    
    if (debug_file) {
        fprintf(debug_file, "  Applied instance properties: Pos=(%d,%d), Size=%dx%d, StyleID=%d\n",
                instance_root->header.pos_x, instance_root->header.pos_y,
                instance_root->header.width, instance_root->header.height,
                instance_root->header.style_id);
    }
}

ComponentInstance* instantiate_component(RenderContext* ctx, RenderElement* placeholder, 
                                        uint8_t component_def_index, FILE* debug_file) {
    if (!ctx || !placeholder || component_def_index >= ctx->doc->header.component_def_count) {
        return NULL;
    }
    
    KrbComponentDefinition* comp_def = &ctx->doc->component_defs[component_def_index];
    
    if (debug_file) {
        fprintf(debug_file, "  Instantiating component: DefIndex=%d, Name='%s'\n", 
                component_def_index, 
                comp_def->name_index < ctx->doc->header.string_count ? 
                ctx->doc->strings[comp_def->name_index] : "UNKNOWN");
    }
    
    // Create component instance tracking
    ComponentInstance* instance = calloc(1, sizeof(ComponentInstance));
    if (!instance) {
        if (debug_file) fprintf(debug_file, "ERROR: Failed to allocate ComponentInstance\n");
        return NULL;
    }
    
    instance->definition_index = component_def_index;
    instance->placeholder = placeholder;
    
    // Create root element from template
    instance->root = create_element_from_template(ctx, &comp_def->root_template_header,
                                                 comp_def->root_template_properties,
                                                 comp_def->root_template_header.property_count,
                                                 debug_file);
    
    if (!instance->root) {
        free(instance);
        return NULL;
    }
    
    // Apply instance-specific properties from placeholder to component root
    apply_instance_properties(instance->root, placeholder, debug_file);
    
    // Mark elements appropriately
    placeholder->is_placeholder = true;
    instance->root->is_component_instance = true;
    instance->root->component_instance = instance;
    
    // Add to context's instance list
    instance->next = ctx->instances;
    ctx->instances = instance;
    
    if (debug_file) {
        fprintf(debug_file, "  Successfully instantiated component\n");
    }
    
    return instance;
}

bool process_component_instances(RenderContext* ctx, FILE* debug_file) {
    if (!ctx || !ctx->doc) return false;
    
    if (debug_file) {
        fprintf(debug_file, "INFO: Processing component instances...\n");
    }
    
    // Iterate through original elements looking for component placeholders
    for (int i = 0; i < ctx->original_element_count; i++) {
        RenderElement* element = &ctx->elements[i];
        
        if (element->custom_prop_count > 0 && element->custom_properties) {
            uint8_t component_name_index;
            
            if (find_component_name_property(element->custom_properties, element->custom_prop_count,
                                           ctx->doc->strings, &component_name_index)) {
                
                if (debug_file) {
                    fprintf(debug_file, "  Found component placeholder: Element %d -> Component name index %d\n", 
                            i, component_name_index);
                }
                
                // Find matching component definition
                bool found_definition = false;
                for (uint8_t def_idx = 0; def_idx < ctx->doc->header.component_def_count; def_idx++) {
                    KrbComponentDefinition* comp_def = &ctx->doc->component_defs[def_idx];
                    if (comp_def->name_index == component_name_index) {
                        // Instantiate this component
                        ComponentInstance* instance = instantiate_component(ctx, element, def_idx, debug_file);
                        if (instance) {
                            found_definition = true;
                            break;
                        }
                    }
                }
                
                if (!found_definition) {
                    if (debug_file) fprintf(debug_file, "ERROR: No component definition found for name index %d\n", component_name_index);
                }
            }
        }
    }
    
    if (debug_file) {
        fprintf(debug_file, "INFO: Finished processing component instances\n");
    }
    
    return true;
}

RenderContext* create_render_context(KrbDocument* doc, FILE* debug_file) {
    if (!doc) return NULL;
    
    RenderContext* ctx = calloc(1, sizeof(RenderContext));
    if (!ctx) return NULL;
    
    ctx->doc = doc;
    ctx->original_element_count = doc->header.element_count;
    ctx->element_count = doc->header.element_count; // Will grow as components are instantiated
    ctx->instances = NULL;
    
    // Allocate initial elements array
    ctx->elements = calloc(doc->header.element_count, sizeof(RenderElement));
    if (!ctx->elements) {
        free(ctx);
        return NULL;
    }
    
    // Set defaults
    ctx->default_bg = BLACK;
    ctx->default_fg = RAYWHITE;
    ctx->default_border = GRAY;
    ctx->window_width = DEFAULT_WINDOW_WIDTH;
    ctx->window_height = DEFAULT_WINDOW_HEIGHT;
    ctx->scale_factor = DEFAULT_SCALE_FACTOR;
    ctx->window_title = NULL;
    ctx->resizable = false;
    
    if (debug_file) {
        fprintf(debug_file, "INFO: Created render context with %d elements\n", ctx->element_count);
    }
    
    return ctx;
}

void free_render_context(RenderContext* ctx) {
    if (!ctx) return;
    
    // Free component instances
    ComponentInstance* instance = ctx->instances;
    while (instance) {
        ComponentInstance* next = instance->next;
        
        // Free instance root element and its children recursively
        if (instance->root) {
            if (instance->root->text) free(instance->root->text);
            if (instance->root->texture_loaded) UnloadTexture(instance->root->texture);
            if (instance->root->custom_properties) {
                for (uint8_t j = 0; j < instance->root->custom_prop_count; j++) {
                    if (instance->root->custom_properties[j].value) {
                        free(instance->root->custom_properties[j].value);
                    }
                }
                free(instance->root->custom_properties);
            }
            free(instance->root);
        }
        
        free(instance);
        instance = next;
    }
    
    // Free elements
    if (ctx->elements) {
        for (int i = 0; i < ctx->element_count; i++) {
            if (ctx->elements[i].text) free(ctx->elements[i].text);
            if (ctx->elements[i].texture_loaded) UnloadTexture(ctx->elements[i].texture);
            if (ctx->elements[i].custom_properties) {
                // Free custom property values
                for (uint8_t j = 0; j < ctx->elements[i].custom_prop_count; j++) {
                    if (ctx->elements[i].custom_properties[j].value) {
                        free(ctx->elements[i].custom_properties[j].value);
                    }
                }
                free(ctx->elements[i].custom_properties);
            }
        }
        free(ctx->elements);
    }
    
    if (ctx->window_title) free(ctx->window_title);
    free(ctx);
}

// --- Rendering Function ---
void render_element(RenderElement* el, int parent_content_x, int parent_content_y, int parent_content_width, int parent_content_height, float scale_factor, FILE* debug_file) {
    if (!el) return;

    // Skip rendering placeholder elements
    if (el->is_placeholder) {
        if (debug_file) {
            fprintf(debug_file, "DEBUG RENDER: Skipping placeholder element %d\n", el->original_index);
        }
        return;
    }

    // --- Calculate Intrinsic Size ---
    int intrinsic_w = (int)(el->header.width * scale_factor);
    int intrinsic_h = (int)(el->header.height * scale_factor);

    if (el->header.type == ELEM_TYPE_TEXT && el->text) {
        int font_size = (int)(BASE_FONT_SIZE * scale_factor); if (font_size < 1) font_size = 1;
        int text_width_measured = (el->text[0] != '\0') ? MeasureText(el->text, font_size) : 0;
        if (el->header.width == 0) intrinsic_w = text_width_measured + (int)(8 * scale_factor); // Add some padding
        if (el->header.height == 0) intrinsic_h = font_size + (int)(8 * scale_factor); // Add some padding
    }
    else if (el->header.type == ELEM_TYPE_IMAGE && el->texture_loaded) {
        if (el->header.width == 0) intrinsic_w = (int)(el->texture.width * scale_factor);
        if (el->header.height == 0) intrinsic_h = (int)(el->texture.height * scale_factor);
    }

    // Clamp minimum size
    if (intrinsic_w < 0) intrinsic_w = 0;
    if (intrinsic_h < 0) intrinsic_h = 0;
    if (el->header.width > 0 && intrinsic_w == 0) intrinsic_w = 1; // Ensure non-zero if specified
    if (el->header.height > 0 && intrinsic_h == 0) intrinsic_h = 1;

    // --- Determine Final Position & Size (Layout) ---
    int final_x, final_y;
    int final_w = intrinsic_w;
    int final_h = intrinsic_h;
    bool has_pos = (el->header.pos_x != 0 || el->header.pos_y != 0); // Explicit position set
    bool is_absolute = (el->header.layout & LAYOUT_ABSOLUTE_BIT);

    // Absolute positioning (or if explicit pos_x/y is set, treat as absolute relative to parent content)
    if (is_absolute || has_pos) {
        final_x = parent_content_x + (int)(el->header.pos_x * scale_factor);
        final_y = parent_content_y + (int)(el->header.pos_y * scale_factor);
    }
    // Flow layout - position determined by parent's layout logic (passed via el->render_x/y)
    else if (el->parent != NULL) {
        final_x = el->render_x; // Use pre-calculated flow position
        final_y = el->render_y;
    }
    // Root element in flow layout - defaults to parent content origin
    else {
        final_x = parent_content_x;
        final_y = parent_content_y;
    }

    // Store final calculated render coordinates
    el->render_x = final_x;
    el->render_y = final_y;
    el->render_w = final_w;
    el->render_h = final_h;

    // --- Apply Styling ---
    Color bg_color = el->bg_color;
    Color fg_color = el->fg_color; // Used for text
    Color border_color = el->border_color;
    int top_bw = (int)(el->border_widths[0] * scale_factor);
    int right_bw = (int)(el->border_widths[1] * scale_factor);
    int bottom_bw = (int)(el->border_widths[2] * scale_factor);
    int left_bw = (int)(el->border_widths[3] * scale_factor);

    // Clamp borders if they exceed element size
    if (el->render_h > 0 && top_bw + bottom_bw >= el->render_h) { top_bw = el->render_h > 1 ? 1 : el->render_h; bottom_bw = 0; }
    if (el->render_w > 0 && left_bw + right_bw >= el->render_w) { left_bw = el->render_w > 1 ? 1 : el->render_w; right_bw = 0; }

    // Debug Logging
    if (debug_file) {
        fprintf(debug_file, "DEBUG RENDER: Elem %d (Type=0x%02X) @(%d,%d) Size=%dx%d Borders=[%d,%d,%d,%d] Layout=0x%02X ResIdx=%d\n",
                el->original_index, el->header.type, el->render_x, el->render_y, el->render_w, el->render_h,
                top_bw, right_bw, bottom_bw, left_bw, el->header.layout, el->resource_index);
    }

    // --- Draw Background (unless it's just text) ---
    bool draw_background = (el->header.type != ELEM_TYPE_TEXT);
    if (draw_background && el->render_w > 0 && el->render_h > 0) {
        DrawRectangle(el->render_x, el->render_y, el->render_w, el->render_h, bg_color);
    }

    // --- Draw Borders ---
    if (el->render_w > 0 && el->render_h > 0) {
        if (top_bw > 0) DrawRectangle(el->render_x, el->render_y, el->render_w, top_bw, border_color);
        if (bottom_bw > 0) DrawRectangle(el->render_x, el->render_y + el->render_h - bottom_bw, el->render_w, bottom_bw, border_color);
        int side_border_y = el->render_y + top_bw;
        int side_border_height = el->render_h - top_bw - bottom_bw; if (side_border_height < 0) side_border_height = 0;
        if (left_bw > 0) DrawRectangle(el->render_x, side_border_y, left_bw, side_border_height, border_color);
        if (right_bw > 0) DrawRectangle(el->render_x + el->render_w - right_bw, side_border_y, right_bw, side_border_height, border_color);
    }

    // --- Calculate Content Area ---
    int content_x = el->render_x + left_bw;
    int content_y = el->render_y + top_bw;
    int content_width = el->render_w - left_bw - right_bw;
    int content_height = el->render_h - top_bw - bottom_bw;
    if (content_width < 0) content_width = 0;
    if (content_height < 0) content_height = 0;

    // --- Draw Content (Text or Image) ---
    if (content_width > 0 && content_height > 0) {
        BeginScissorMode(content_x, content_y, content_width, content_height);

        // Draw Text
        if ((el->header.type == ELEM_TYPE_TEXT || el->header.type == ELEM_TYPE_BUTTON) && el->text && el->text[0] != '\0') {
            int font_size = (int)(BASE_FONT_SIZE * scale_factor); if (font_size < 1) font_size = 1;
            int text_width_measured = MeasureText(el->text, font_size);
            int text_draw_x = content_x;
            if (el->text_alignment == 1) text_draw_x = content_x + (content_width - text_width_measured) / 2; // Center
            else if (el->text_alignment == 2) text_draw_x = content_x + content_width - text_width_measured;   // End/Right
            int text_draw_y = content_y + (content_height - font_size) / 2; // Vertical center

            if (text_draw_x < content_x) text_draw_x = content_x; // Clamp
            if (text_draw_y < content_y) text_draw_y = content_y; // Clamp

            if (debug_file) fprintf(debug_file, "  -> Drawing Text (Type %02X) '%s' (align=%d) at (%d,%d) within content (%d,%d %dx%d)\n", el->header.type, el->text, el->text_alignment, text_draw_x, text_draw_y, content_x, content_y, content_width, content_height);
            DrawText(el->text, text_draw_x, text_draw_y, font_size, fg_color);
        }
        
        // Draw Image
        else if (el->header.type == ELEM_TYPE_IMAGE && el->texture_loaded) {
             if (debug_file) fprintf(debug_file, "  -> Drawing Image Texture (ResIdx %d) within content (%d,%d %dx%d)\n", el->resource_index, content_x, content_y, content_width, content_height);
             // Simple stretch draw for now
             Rectangle sourceRec = { 0.0f, 0.0f, (float)el->texture.width, (float)el->texture.height };
             Rectangle destRec = { (float)content_x, (float)content_y, (float)content_width, (float)content_height };
             Vector2 origin = { 0.0f, 0.0f };
             DrawTexturePro(el->texture, sourceRec, destRec, origin, 0.0f, WHITE);
        }

        EndScissorMode();
    }

    // --- Layout and Render Children ---
    if (el->child_count > 0 && content_width > 0 && content_height > 0) {
        uint8_t direction = el->header.layout & LAYOUT_DIRECTION_MASK; // 00=Row, 01=Col, etc.
        uint8_t alignment = (el->header.layout & LAYOUT_ALIGNMENT_MASK) >> 2; // 00=Start, 01=Center, etc.
        int current_flow_x = content_x;
        int current_flow_y = content_y;
        int total_child_width_scaled = 0;  // Total width of flow children in this row/column
        int total_child_height_scaled = 0; // Total height of flow children in this row/column
        int flow_child_count = 0;          // Number of children participating in flow layout
        int child_sizes[MAX_ELEMENTS][2];  // Store calculated sizes [width, height]

        if (debug_file) fprintf(debug_file, "  Layout Children of Elem %d: Count=%d Dir=%d Align=%d Content=(%d,%d %dx%d)\n", el->original_index, el->child_count, direction, alignment, content_x, content_y, content_width, content_height);

        // Pass 1: Calculate sizes and total dimensions of flow children
        for (int i = 0; i < el->child_count; i++) {
            RenderElement* child = el->children[i];
            if (!child) continue;
            
            // Skip placeholder children
            if (child->is_placeholder) {
                child_sizes[i][0] = 0; child_sizes[i][1] = 0;
                continue;
            }
            
            bool child_is_absolute = (child->header.layout & LAYOUT_ABSOLUTE_BIT);
            bool child_has_pos = (child->header.pos_x != 0 || child->header.pos_y != 0);

            // Skip absolute/positioned children for flow calculations
            if (child_is_absolute || child_has_pos) {
                child_sizes[i][0] = 0; child_sizes[i][1] = 0;
                continue;
            }

            // Calculate child intrinsic size (similar logic to parent)
            int child_w = (int)(child->header.width * scale_factor);
            int child_h = (int)(child->header.height * scale_factor);
            if (child->header.type == ELEM_TYPE_TEXT && child->text) {
                int fs = (int)(BASE_FONT_SIZE * scale_factor); if(fs<1)fs=1;
                int tw = (child->text[0]!='\0') ? MeasureText(child->text, fs):0;
                if (child->header.width == 0) child_w = tw + (int)(8 * scale_factor);
                if (child->header.height == 0) child_h = fs + (int)(8 * scale_factor);
            }
            else if (child->header.type == ELEM_TYPE_IMAGE && child->texture_loaded) {
                if (child->header.width == 0) child_w = (int)(child->texture.width * scale_factor);
                if (child->header.height == 0) child_h = (int)(child->texture.height * scale_factor);
            }
            // Clamp and ensure minimum size
            if (child_w < 0) child_w = 0;
            if (child_h < 0) child_h = 0;
            if (child->header.width > 0 && child_w == 0) child_w = 1;
            if (child->header.height > 0 && child_h == 0) child_h = 1;

            child_sizes[i][0] = child_w;
            child_sizes[i][1] = child_h;

            // Accumulate total size based on flow direction
            if (direction == 0x00 || direction == 0x02) { // Row or RowReverse
                total_child_width_scaled += child_w;
            } else { // Column or ColumnReverse
                total_child_height_scaled += child_h;
            }
            flow_child_count++;
        }

        // Pass 2: Calculate starting position based on alignment
        if (direction == 0x00 || direction == 0x02) { // Row flow
            if (alignment == 0x01) { current_flow_x = content_x + (content_width - total_child_width_scaled) / 2; } // Center
            else if (alignment == 0x02) { current_flow_x = content_x + content_width - total_child_width_scaled; }   // End
            // else: Start (default)
            if (current_flow_x < content_x) current_flow_x = content_x; // Clamp
        } else { // Column flow
            if (alignment == 0x01) { current_flow_y = content_y + (content_height - total_child_height_scaled) / 2; } // Center
            else if (alignment == 0x02) { current_flow_y = content_y + content_height - total_child_height_scaled; }   // End
            // else: Start (default)
            if (current_flow_y < content_y) current_flow_y = content_y; // Clamp
        }

        // Calculate spacing for SpaceBetween
        float space_between = 0;
        if (alignment == 0x03 && flow_child_count > 1) { // SpaceBetween
            if (direction == 0x00 || direction == 0x02) { // Row
                space_between = (float)(content_width - total_child_width_scaled) / (flow_child_count - 1);
            } else { // Column
                space_between = (float)(content_height - total_child_height_scaled) / (flow_child_count - 1);
            }
            if (space_between < 0) space_between = 0; // Avoid negative spacing
        }

        // Pass 3: Position and render children
        int flow_children_processed = 0;
        for (int i = 0; i < el->child_count; i++) {
            RenderElement* child = el->children[i];
            if (!child) continue;
            
            // Skip placeholder children
            if (child->is_placeholder) continue;

            bool child_is_absolute = (child->header.layout & LAYOUT_ABSOLUTE_BIT);
            bool child_has_pos = (child->header.pos_x != 0 || child->header.pos_y != 0);

            // Render absolutely positioned children directly relative to parent content area
            if (child_is_absolute || child_has_pos) {
                render_element(child, content_x, content_y, content_width, content_height, scale_factor, debug_file);
            }
            // Position and render flow children
            else {
                int child_w = child_sizes[i][0];
                int child_h = child_sizes[i][1];
                int child_final_x, child_final_y;

                // Determine position based on flow direction and alignment
                if (direction == 0x00 || direction == 0x02) { // Row Flow
                    child_final_x = current_flow_x;
                    // Vertical alignment within the row
                    if (alignment == 0x01) child_final_y = content_y + (content_height - child_h) / 2; // Center
                    else if (alignment == 0x02) child_final_y = content_y + content_height - child_h; // End (bottom)
                    else child_final_y = content_y; // Start (top)
                } else { // Column Flow
                    child_final_y = current_flow_y;
                    // Horizontal alignment within the column
                    if (alignment == 0x01) child_final_x = content_x + (content_width - child_w) / 2; // Center
                    else if (alignment == 0x02) child_final_x = content_x + content_width - child_w; // End (right)
                    else child_final_x = content_x; // Start (left)
                }

                // Assign calculated position to child before rendering
                child->render_x = child_final_x;
                child->render_y = child_final_y;

                render_element(child, content_x, content_y, content_width, content_height, scale_factor, debug_file);

                // Advance flow position for the next child
                if (direction == 0x00 || direction == 0x02) { // Row
                    current_flow_x += child_w;
                    if (alignment == 0x03 && flow_children_processed < flow_child_count - 1) {
                        current_flow_x += (int)roundf(space_between); // Add space between
                    }
                } else { // Column
                    current_flow_y += child_h;
                    if (alignment == 0x03 && flow_children_processed < flow_child_count - 1) {
                        current_flow_y += (int)roundf(space_between); // Add space between
                    }
                }
                flow_children_processed++;
            }
        }
    } // End child rendering

    if (debug_file) fprintf(debug_file, "  Finished Render Elem %d\n", el->original_index);
}

// --- Standalone Main Application Logic ---
#ifdef BUILD_STANDALONE_RENDERER

int main(int argc, char* argv[]) {
    // --- Setup ---
    if (argc != 2) { printf("Usage: %s <krb_file>\n", argv[0]); return 1; }
    const char* krb_file_path = argv[1];
    char* krb_file_path_copy = strdup(krb_file_path);
    if (!krb_file_path_copy) { perror("Failed to duplicate krb_file_path"); return 1; }
    const char* krb_dir = dirname(krb_file_path_copy);

    FILE* debug_file = fopen("krb_render_debug_standalone.log", "w");
    if (!debug_file) { debug_file = stderr; fprintf(stderr, "Warn: No debug log.\n"); }
    setvbuf(debug_file, NULL, _IOLBF, BUFSIZ);
    fprintf(debug_file, "INFO: Opening KRB: %s\n", krb_file_path);
    fprintf(debug_file, "INFO: Base Directory: %s\n", krb_dir);
    FILE* file = fopen(krb_file_path, "rb");
    if (!file) { 
        fprintf(stderr, "ERROR: Cannot open '%s': %s\n", krb_file_path, strerror(errno)); 
        free(krb_file_path_copy); 
        if (debug_file != stderr) fclose(debug_file); 
        return 1; 
    }

    // --- Parsing ---
    KrbDocument doc = {0};
    fprintf(debug_file, "INFO: Reading KRB document...\n");
    if (!krb_read_document(file, &doc)) {
        fprintf(stderr, "ERROR: Failed parse KRB '%s'\n", krb_file_path);
        fclose(file); krb_free_document(&doc); free(krb_file_path_copy); 
        if (debug_file != stderr) fclose(debug_file);
        return 1;
    }
    fclose(file);
    fprintf(debug_file, "INFO: Parsed KRB OK - Ver=%u.%u Elements=%u ComponentDefs=%u Styles=%u Strings=%u Resources=%u Flags=0x%04X\n",
            doc.version_major, doc.version_minor, doc.header.element_count, 
            doc.header.component_def_count, doc.header.style_count, 
            doc.header.string_count, doc.header.resource_count, doc.header.flags);
    
    if (doc.header.element_count == 0) {
        fprintf(debug_file, "WARN: No elements. Exiting.\n");
        krb_free_document(&doc); free(krb_file_path_copy); 
        if (debug_file != stderr) fclose(debug_file);
        return 0;
    }
    
    if (doc.version_major != KRB_SPEC_VERSION_MAJOR || doc.version_minor != KRB_SPEC_VERSION_MINOR) {
         fprintf(stderr, "WARN: KRB version mismatch! Doc is %d.%d, Reader expects %d.%d. Parsing continues...\n",
                 doc.version_major, doc.version_minor, KRB_SPEC_VERSION_MAJOR, KRB_SPEC_VERSION_MINOR);
         fprintf(debug_file, "WARN: KRB version mismatch! Doc is %d.%d, Reader expects %d.%d.\n",
                 doc.version_major, doc.version_minor, KRB_SPEC_VERSION_MAJOR, KRB_SPEC_VERSION_MINOR);
     }

    // --- Create Render Context ---
    RenderContext* ctx = create_render_context(&doc, debug_file);
    if (!ctx) {
        fprintf(stderr, "ERROR: Failed to create render context\n");
        krb_free_document(&doc); free(krb_file_path_copy);
        if (debug_file != stderr) fclose(debug_file);
        return 1;
    }

    // --- Process App & Defaults ---
    RenderElement* app_element = NULL;

    if ((doc.header.flags & FLAG_HAS_APP) && doc.header.element_count > 0 && doc.elements[0].type == ELEM_TYPE_APP) {
        app_element = &ctx->elements[0];
        app_element->header = doc.elements[0]; 
        app_element->original_index = 0; 
        app_element->text = NULL;
        app_element->parent = NULL; 
        app_element->child_count = 0; 
        app_element->texture_loaded = false;
        app_element->resource_index = INVALID_RESOURCE_INDEX;
        app_element->is_placeholder = false;
        app_element->is_component_instance = false;
        app_element->component_instance = NULL;
        app_element->custom_properties = NULL;
        app_element->custom_prop_count = 0;
        
        for(int k=0; k<MAX_ELEMENTS; ++k) app_element->children[k] = NULL; 
        app_element->is_interactive = false;
        
        fprintf(debug_file, "INFO: Processing App Elem 0 (StyleID=%d, Props=%d)\n", 
                app_element->header.style_id, app_element->header.property_count);
        
        // Apply App Style
        if (app_element->header.style_id > 0 && app_element->header.style_id <= doc.header.style_count && doc.styles) {
             int style_idx = app_element->header.style_id - 1; 
             KrbStyle* app_style = &doc.styles[style_idx];
             fprintf(debug_file, "  Applying App Style %d\n", style_idx);
             for(int j=0; j<app_style->property_count; ++j) { 
                 apply_property_to_element(app_element, &app_style->properties[j], &doc, debug_file);
             }
        } else if (app_element->header.style_id > 0) { 
            fprintf(debug_file, "WARN: App Style ID %d invalid.\n", app_element->header.style_id); 
        }
        
        app_element->bg_color = ctx->default_bg; 
        app_element->fg_color = ctx->default_fg; 
        app_element->border_color = ctx->default_border; 
        memset(app_element->border_widths, 0, 4);
        
        // Apply App Direct Properties
        fprintf(debug_file, "  Applying App Direct Props\n");
        if (doc.properties && doc.properties[0]) {
            for (int j = 0; j < app_element->header.property_count; j++) { 
                KrbProperty* prop = &doc.properties[0][j]; 
                if (!prop || !prop->value) continue; 
                
                if (prop->property_id == PROP_ID_WINDOW_WIDTH && prop->value_type == VAL_TYPE_SHORT && prop->size == 2) { 
                    ctx->window_width = krb_read_u16_le(prop->value); 
                    app_element->header.width = ctx->window_width; 
                } else if (prop->property_id == PROP_ID_WINDOW_HEIGHT && prop->value_type == VAL_TYPE_SHORT && prop->size == 2) { 
                    ctx->window_height = krb_read_u16_le(prop->value); 
                    app_element->header.height = ctx->window_height; 
                } else if (prop->property_id == PROP_ID_WINDOW_TITLE && prop->value_type == VAL_TYPE_STRING && prop->size == 1) { 
                    uint8_t idx = *(uint8_t*)prop->value; 
                    if (idx < doc.header.string_count && doc.strings[idx]) { 
                        free(ctx->window_title); 
                        ctx->window_title = strdup(doc.strings[idx]); 
                    } 
                } else if (prop->property_id == PROP_ID_RESIZABLE && prop->value_type == VAL_TYPE_BYTE && prop->size == 1) { 
                    ctx->resizable = *(uint8_t*)prop->value; 
                } else if (prop->property_id == PROP_ID_SCALE_FACTOR && prop->value_type == VAL_TYPE_PERCENTAGE && prop->size == 2) { 
                    uint16_t sf = krb_read_u16_le(prop->value); 
                    ctx->scale_factor = sf / 256.0f; 
                } else {
                    apply_property_to_element(app_element, prop, &doc, debug_file);
                }
            }
        }
        app_element->render_w = ctx->window_width; 
        app_element->render_h = ctx->window_height; 
        app_element->render_x = 0; 
        app_element->render_y = 0;
        
        fprintf(debug_file, "INFO: Processed App. Window:%dx%d Title:'%s' Scale:%.2f\n", 
                ctx->window_width, ctx->window_height, 
                ctx->window_title ? ctx->window_title : "(None)", ctx->scale_factor);
    } else { 
        fprintf(debug_file, "WARN: No App element. Using defaults.\n"); 
        ctx->window_title = strdup("KRB Renderer (No App)"); 
    }

    // --- Populate & Process Remaining RenderElements ---
    for (int i = 0; i < doc.header.element_count; i++) {
        if (app_element && i == 0) continue;
        
        RenderElement* current_render_el = &ctx->elements[i];
        current_render_el->header = doc.elements[i]; 
        current_render_el->original_index = i; 
        current_render_el->text = NULL;
        current_render_el->bg_color = ctx->default_bg; 
        current_render_el->fg_color = ctx->default_fg; 
        current_render_el->border_color = ctx->default_border;
        memset(current_render_el->border_widths, 0, 4); 
        current_render_el->text_alignment = 0; 
        current_render_el->parent = NULL;
        current_render_el->child_count = 0; 
        current_render_el->texture_loaded = false;
        current_render_el->resource_index = INVALID_RESOURCE_INDEX;
        current_render_el->is_placeholder = false;
        current_render_el->is_component_instance = false;
        current_render_el->component_instance = NULL;
        current_render_el->custom_properties = NULL;
        current_render_el->custom_prop_count = 0;
        
        for(int k=0; k<MAX_ELEMENTS; ++k) current_render_el->children[k] = NULL;
        current_render_el->render_x = 0; 
        current_render_el->render_y = 0; 
        current_render_el->render_w = 0; 
        current_render_el->render_h = 0;
        current_render_el->is_interactive = (current_render_el->header.type == ELEM_TYPE_BUTTON || current_render_el->header.type == ELEM_TYPE_INPUT);
        
        // Copy custom properties if present
        if (doc.elements[i].custom_prop_count > 0 && doc.custom_properties && doc.custom_properties[i]) {
            current_render_el->custom_prop_count = doc.elements[i].custom_prop_count;
            current_render_el->custom_properties = calloc(current_render_el->custom_prop_count, sizeof(KrbCustomProperty));
            if (current_render_el->custom_properties) {
                for (uint8_t j = 0; j < current_render_el->custom_prop_count; j++) {
                    current_render_el->custom_properties[j] = doc.custom_properties[i][j];
                    // Note: We're sharing the value pointers - be careful with cleanup
                }
            }
        }
        
        fprintf(debug_file, "INFO: Processing Elem %d (Type=0x%02X, StyleID=%d, Props=%d, CustomProps=%d)\n", 
                i, current_render_el->header.type, current_render_el->header.style_id, 
                current_render_el->header.property_count, current_render_el->custom_prop_count);
        
        // Apply Style
        if (current_render_el->header.style_id > 0 && current_render_el->header.style_id <= doc.header.style_count && doc.styles) {
            int style_idx = current_render_el->header.style_id - 1; 
            KrbStyle* style = &doc.styles[style_idx];
            fprintf(debug_file, "  Applying Style %d (Props=%d)\n", style_idx, style->property_count);
            for(int j=0; j<style->property_count; ++j) { 
                apply_property_to_element(current_render_el, &style->properties[j], &doc, debug_file);
            }
        } else if (current_render_el->header.style_id > 0) { 
            fprintf(debug_file, "WARN: Style ID %d invalid.\n", current_render_el->header.style_id); 
        }
        
        // Apply Direct Properties
        fprintf(debug_file, "  Applying Direct Props (Count=%d)\n", current_render_el->header.property_count);
        if (doc.properties && i < doc.header.element_count && doc.properties[i]) {
             for (int j = 0; j < current_render_el->header.property_count; j++) { 
                 apply_property_to_element(current_render_el, &doc.properties[i][j], &doc, debug_file);
             }
        }
        
        fprintf(debug_file, "  Finished Elem %d. Text='%s' Align=%d ResIdx=%d\n", 
                i, current_render_el->text ? current_render_el->text : "NULL", 
                current_render_el->text_alignment, current_render_el->resource_index);
    }

    // --- Process Component Instances ---
    if (!process_component_instances(ctx, debug_file)) {
        fprintf(stderr, "ERROR: Failed to process component instances\n");
        free_render_context(ctx);
        krb_free_document(&doc); free(krb_file_path_copy);
        if (debug_file != stderr) fclose(debug_file);
        return 1;
    }

    // --- Build Parent/Child Tree ---
    fprintf(debug_file, "INFO: Building element tree...\n");
    RenderElement* parent_stack[MAX_ELEMENTS]; 
    int stack_top = -1;
    
    // Build tree for original elements
    for (int i = 0; i < doc.header.element_count; i++) {
        RenderElement* current_el = &ctx->elements[i];
        
        // Skip placeholder elements in tree building
        if (current_el->is_placeholder) continue;
        
        while (stack_top >= 0) { 
            RenderElement* p = parent_stack[stack_top]; 
            if (p->child_count >= p->header.child_count) 
                stack_top--; 
            else 
                break; 
        }
        
        if (stack_top >= 0) { 
            RenderElement* p = parent_stack[stack_top]; 
            current_el->parent = p; 
            if (p->child_count < MAX_ELEMENTS) 
                p->children[p->child_count++] = current_el; 
            else 
                fprintf(debug_file, "WARN: Max children parent %d.\n", p->original_index); 
        }
        
        if (current_el->header.child_count > 0) { 
            if (stack_top + 1 < MAX_ELEMENTS) 
                parent_stack[++stack_top] = current_el; 
            else 
                fprintf(debug_file, "WARN: Max stack depth elem %d.\n", i); 
        }
    }
    
    // Add component instance roots to the tree
    ComponentInstance* instance = ctx->instances;
    while (instance) {
        if (instance->root && instance->placeholder) {
            // Replace placeholder with component instance in parent's children
            if (instance->placeholder->parent) {
                RenderElement* parent = instance->placeholder->parent;
                for (int i = 0; i < parent->child_count; i++) {
                    if (parent->children[i] == instance->placeholder) {
                        parent->children[i] = instance->root;
                        instance->root->parent = parent;
                        break;
                    }
                }
            }
        }
        instance = instance->next;
    }
    
    fprintf(debug_file, "INFO: Finished building element tree.\n");

    // --- Find Roots ---
    RenderElement* root_elements[MAX_ELEMENTS]; 
    int root_count = 0;
    
    for(int i = 0; i < doc.header.element_count; ++i) { 
        if (!ctx->elements[i].parent && !ctx->elements[i].is_placeholder) { 
            if (root_count < MAX_ELEMENTS) 
                root_elements[root_count++] = &ctx->elements[i]; 
            else { 
                fprintf(debug_file, "WARN: Max roots.\n"); 
                break; 
            } 
        } 
    }
    
    // Add component instance roots that are not already included
    instance = ctx->instances;
    while (instance && root_count < MAX_ELEMENTS) {
        if (instance->root && !instance->root->parent) {
            root_elements[root_count++] = instance->root;
        }
        instance = instance->next;
    }
    
    if (root_count == 0 && doc.header.element_count > 0) { 
        fprintf(stderr, "ERROR: No root found!\n"); 
        fprintf(debug_file, "ERROR: No root!\n"); 
        free_render_context(ctx);
        krb_free_document(&doc); 
        free(krb_file_path_copy); 
        if(debug_file!=stderr) fclose(debug_file); 
        return 1; 
    }
    
    fprintf(debug_file, "INFO: Found %d root(s).\n", root_count);

    // --- Init Raylib Window ---
    fprintf(debug_file, "INFO: Init window %dx%d Title: '%s'\n", 
            ctx->window_width, ctx->window_height, 
            ctx->window_title ? ctx->window_title : "KRB Renderer");
    InitWindow(ctx->window_width, ctx->window_height, 
               ctx->window_title ? ctx->window_title : "KRB Renderer");
    if (ctx->resizable) SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetTargetFPS(60);

    // --- Load Textures ---
    fprintf(debug_file, "INFO: Loading textures...\n");
    for (int i = 0; i < doc.header.element_count; ++i) {
        RenderElement* el = &ctx->elements[i];
        if (el->header.type == ELEM_TYPE_IMAGE && el->resource_index != INVALID_RESOURCE_INDEX) {
            if (el->resource_index >= doc.header.resource_count || !doc.resources) { 
                fprintf(stderr, "ERROR: Elem %d invalid res idx %d (max %d).\n", 
                        i, el->resource_index, doc.header.resource_count - 1); 
                continue; 
            }
            
            KrbResource* res = &doc.resources[el->resource_index];
            if (res->format == RES_FORMAT_EXTERNAL) {
                if (res->data_string_index >= doc.header.string_count || !doc.strings || !doc.strings[res->data_string_index]) { 
                    fprintf(stderr, "ERROR: Res %d invalid data str idx %d.\n", 
                            el->resource_index, res->data_string_index); 
                    continue; 
                }
                
                const char* relative_path = doc.strings[res->data_string_index];
                char full_path[MAX_LINE_LENGTH];
                
                if (strcmp(krb_dir, ".") == 0) {
                    snprintf(full_path, sizeof(full_path), "%s", relative_path);
                } else {
                    snprintf(full_path, sizeof(full_path), "%s/%s", krb_dir, relative_path);
                }
                full_path[sizeof(full_path) - 1] = '\0';

                fprintf(debug_file, "  Loading texture Elem %d (Res %d): '%s' (Relative: '%s')\n", 
                        i, el->resource_index, full_path, relative_path);

                el->texture = LoadTexture(full_path);
                if (IsTextureReady(el->texture)) {
                    el->texture_loaded = true;
                    fprintf(debug_file, "    -> OK (ID: %u, %dx%d)\n", 
                            el->texture.id, el->texture.width, el->texture.height);
                } else {
                    fprintf(stderr, "ERROR: Failed load texture: %s\n", full_path);
                    fprintf(debug_file, "    -> FAILED: %s\n", full_path);
                    el->texture_loaded = false;
                }
            } else {
                fprintf(debug_file, "WARN: Inline res NI Elem %d (Res %d).\n", i, el->resource_index);
            }
        }
    }
    
    // Load textures for component instances
    instance = ctx->instances;
    while (instance) {
        if (instance->root && instance->root->header.type == ELEM_TYPE_IMAGE && 
            instance->root->resource_index != INVALID_RESOURCE_INDEX) {
            // Similar texture loading logic as above
            // (Implementation would be similar to the loop above)
        }
        instance = instance->next;
    }
    
    fprintf(debug_file, "INFO: Finished loading textures.\n");

    // --- Main Loop ---
    fprintf(debug_file, "INFO: Entering main loop...\n");
    while (!WindowShouldClose()) {
        Vector2 mousePos = GetMousePosition();
        if (ctx->resizable && IsWindowResized()) { 
            ctx->window_width = GetScreenWidth(); 
            ctx->window_height = GetScreenHeight(); 
            if (app_element && app_element->parent == NULL) { 
                app_element->render_w = ctx->window_width; 
                app_element->render_h = ctx->window_height; 
            } 
            fprintf(debug_file, "INFO: Resized %dx%d.\n", ctx->window_width, ctx->window_height); 
        }
        
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        for (int i = doc.header.element_count - 1; i >= 0; --i) { 
            RenderElement* el = &ctx->elements[i]; 
            if (el->is_interactive && el->render_w > 0 && el->render_h > 0 && !el->is_placeholder) { 
                Rectangle r = { (float)el->render_x, (float)el->render_y, (float)el->render_w, (float)el->render_h }; 
                if (CheckCollisionPointRec(mousePos, r)) { 
                    SetMouseCursor(MOUSE_CURSOR_POINTING_HAND); 
                    break; 
                } 
            } 
        }

        BeginDrawing();
        Color clear_color = (app_element) ? app_element->bg_color : BLACK; 
        ClearBackground(clear_color);
        fflush(debug_file);
        
        if (root_count > 0) {
            for (int i = 0; i < root_count; ++i) { 
                if (root_elements[i]) 
                    render_element(root_elements[i], 0, 0, ctx->window_width, ctx->window_height, ctx->scale_factor, debug_file); 
                else 
                    fprintf(debug_file, "WARN: Root %d NULL.\n", i); 
            }
        } else { 
            DrawText("No roots.", 10, 10, 20, RED); 
            fprintf(debug_file, "--- FRAME SKIPPED (No roots) ---\n"); 
        }
        
        EndDrawing();
    }

    // --- Cleanup ---
    fprintf(debug_file, "INFO: Closing & cleanup...\n"); 
    CloseWindow();
    fprintf(debug_file, "INFO: Cleanup complete.\n");
    
    free_render_context(ctx);
    krb_free_document(&doc);
    free(krb_file_path_copy);
    if (debug_file != stderr) { fclose(debug_file); }
    printf("Standalone renderer finished.\n");
    return 0;
}

#endif // BUILD_STANDALONE_RENDERER