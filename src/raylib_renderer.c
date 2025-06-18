#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <math.h> 
#include <libgen.h> 

#include "custom_components.h"
#include "custom_tabbar.h"
#include "renderer.h" 

// --- Basic Definitions ---
#define DEFAULT_WINDOW_WIDTH 800
#define DEFAULT_WINDOW_HEIGHT 600
#define DEFAULT_SCALE_FACTOR 1.0f
#define BASE_FONT_SIZE 20

// --- Forward Declarations ---
void initialize_render_element(RenderElement* el, KrbElementHeader* header, int index, RenderContext* ctx);
bool expand_all_components(RenderContext* ctx, FILE* debug_file);
bool expand_component_for_element(RenderContext* ctx, RenderElement* element, uint8_t component_name_index, FILE* debug_file);
void apply_property_inheritance(RenderContext* ctx, FILE* debug_file);
void inherit_properties_recursive(RenderElement* el, RenderContext* ctx, FILE* debug_file);
void find_root_elements(RenderContext* ctx, FILE* debug_file);
void process_app_element_properties(RenderElement* app_element, KrbDocument* doc, RenderContext* ctx, FILE* debug_file);
void apply_element_styling(RenderElement* el, KrbDocument* doc, RenderContext* ctx, FILE* debug_file);
void build_element_tree(RenderContext* ctx, FILE* debug_file);
void load_all_textures(RenderContext* ctx, const char* base_dir, FILE* debug_file);
void handle_window_resize(RenderContext* ctx);


static bool g_cursor_set_this_frame = false;
static int g_highest_cursor_priority = -1;

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
                    if (debug_file) {
                        fprintf(debug_file, "    -> Applied text: '%s' to element\n", element->text);
                    }
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
            
        case PROP_ID_VISIBILITY:
            if (prop->value_type == VAL_TYPE_BYTE && prop->size == 1) {
                element->is_visible = (*(uint8_t*)prop->value != 0);
                if (debug_file) {
                    fprintf(debug_file, "    -> Applied visibility: %s to element\n", 
                            element->is_visible ? "true" : "false");
                }
            }
            break;

        case PROP_ID_FONT_SIZE:
            if (prop->value_type == VAL_TYPE_SHORT && prop->size == 2) {
                uint16_t font_size = krb_read_u16_le(prop->value);
                element->font_size = (float)font_size;
                if (debug_file) {
                    fprintf(debug_file, "    -> Applied font size: %.1f to element\n", element->font_size);
                }
            }
            break;
            
        default:
            // Unknown property, ignore
            break;
    }
}
void calculate_element_minimum_size(RenderElement* el, float scale_factor) {
    if (!el) return;
    
    int min_w = 0, min_h = 0;
    bool should_inherit_parent_size = false;
    
    // Check if element should inherit parent size
    if (el->header.type == ELEM_TYPE_CONTAINER || el->header.type == ELEM_TYPE_APP) {
        bool has_grow = (el->header.layout & LAYOUT_GROW_BIT) != 0;
        bool has_explicit_width = (el->header.width > 0);
        bool has_explicit_height = (el->header.height > 0);
        
        should_inherit_parent_size = has_grow || 
            ((!has_explicit_width || !has_explicit_height) && el->parent != NULL);
    }
    
    // Calculate intrinsic content size
    if (el->header.type == ELEM_TYPE_TEXT && el->text && el->text[0] != '\0') {
        // CRITICAL FIX: Force proper text sizing with debugging
        float font_size = (el->font_size > 0) ? el->font_size : BASE_FONT_SIZE;
        int scaled_font_size = (int)(font_size * scale_factor);
        if (scaled_font_size < 1) scaled_font_size = 1;
        
        int text_width_measured = MeasureText(el->text, scaled_font_size);
        min_w = text_width_measured + (int)(8 * scale_factor);
        min_h = scaled_font_size + (int)(8 * scale_factor);
        
        // FORCE the calculated size - this is the critical fix
        el->render_w = min_w;
        el->render_h = min_h;
        
        printf("TEXT SIZE DEBUG: '%s' font_size=%.1f scaled=%d measured_width=%d final_size=%dx%d\n", 
               el->text, font_size, scaled_font_size, text_width_measured, min_w, min_h);
        
        return; // Skip the normal size application logic for text
    }
    else if (el->header.type == ELEM_TYPE_BUTTON && el->text && el->text[0] != '\0') {
        float font_size = (el->font_size > 0) ? el->font_size : BASE_FONT_SIZE;
        int scaled_font_size = (int)(font_size * scale_factor);
        if (scaled_font_size < 1) scaled_font_size = 1;
        int text_width_measured = MeasureText(el->text, scaled_font_size);
        min_w = text_width_measured + (int)(16 * scale_factor);
        min_h = scaled_font_size + (int)(16 * scale_factor);
    }
    else if (el->header.type == ELEM_TYPE_IMAGE && el->texture_loaded) {
        min_w = (int)(el->texture.width * scale_factor);
        min_h = (int)(el->texture.height * scale_factor);
    }
    else if (should_inherit_parent_size && el->parent) {
        min_w = el->parent->render_w > 0 ? el->parent->render_w : (int)(100 * scale_factor);
        min_h = el->parent->render_h > 0 ? el->parent->render_h : (int)(100 * scale_factor);
    }
    else if (el->header.type == ELEM_TYPE_CONTAINER || el->header.type == ELEM_TYPE_APP) {
        min_w = (int)(100 * scale_factor);
        min_h = (int)(100 * scale_factor);
    }
    
    // Apply explicit sizes or use calculated values (for non-text elements)
    if (el->header.width > 0) {
        el->render_w = (int)(el->header.width * scale_factor);
    } else if (should_inherit_parent_size && el->parent) {
        el->render_w = el->parent->render_w;
    } else {
        el->render_w = min_w;
    }
    
    if (el->header.height > 0) {
        el->render_h = (int)(el->header.height * scale_factor);
    } else if (should_inherit_parent_size && el->parent) {
        el->render_h = el->parent->render_h;
    } else {
        el->render_h = min_h;
    }
    
    // Ensure minimum size of 1x1 for visible elements
    if (el->render_w <= 0) el->render_w = 1;
    if (el->render_h <= 0) el->render_h = 1;
}

void initialize_render_element(RenderElement* el, KrbElementHeader* header, int index, RenderContext* ctx) {
    el->header = *header;
    el->original_index = index;
    el->text = NULL;
    el->bg_color = (Color){0, 0, 0, 0}; // Transparent
    el->fg_color = (Color){0, 0, 0, 0}; // Unset - will inherit
    el->border_color = (Color){0, 0, 0, 0}; // Transparent
    memset(el->border_widths, 0, 4);
    el->text_alignment = 0; // Will inherit
    el->parent = NULL;
    el->child_count = 0;
    el->texture_loaded = false;
    el->resource_index = INVALID_RESOURCE_INDEX;
    el->is_placeholder = false;
    el->is_component_instance = false;
    el->component_instance = NULL;
    el->custom_properties = NULL;
    el->custom_prop_count = 0;
    el->is_visible = true; // Default visible
    el->is_interactive = (header->type == ELEM_TYPE_BUTTON || header->type == ELEM_TYPE_INPUT);
    el->font_size = 0.0f; // Will inherit
    
    for(int k = 0; k < MAX_ELEMENTS; k++) el->children[k] = NULL;
    el->render_x = 0; el->render_y = 0; el->render_w = 0; el->render_h = 0;
}

void process_app_element_properties(RenderElement* app_element, KrbDocument* doc, RenderContext* ctx, FILE* debug_file) {
    if (!app_element || !doc || !ctx) return;
    
    // Apply App Style if present
    if (app_element->header.style_id > 0 && app_element->header.style_id <= doc->header.style_count && doc->styles) {
        int style_idx = app_element->header.style_id - 1; 
        KrbStyle* app_style = &doc->styles[style_idx];
        for(int j = 0; j < app_style->property_count; j++) { 
            apply_property_to_element(app_element, &app_style->properties[j], doc, debug_file);
        }
    }
    
    // Apply App Direct Properties for window configuration
    if (doc->properties && doc->properties[0]) {
        for (int j = 0; j < app_element->header.property_count; j++) { 
            KrbProperty* prop = &doc->properties[0][j]; 
            if (!prop || !prop->value) continue; 
            
            switch (prop->property_id) {
                case PROP_ID_WINDOW_WIDTH:
                    if (prop->value_type == VAL_TYPE_SHORT && prop->size == 2) { 
                        ctx->window_width = krb_read_u16_le(prop->value); 
                        app_element->header.width = ctx->window_width; 
                    }
                    break;
                case PROP_ID_WINDOW_HEIGHT:
                    if (prop->value_type == VAL_TYPE_SHORT && prop->size == 2) { 
                        ctx->window_height = krb_read_u16_le(prop->value); 
                        app_element->header.height = ctx->window_height; 
                    }
                    break;
                case PROP_ID_WINDOW_TITLE:
                    if (prop->value_type == VAL_TYPE_STRING && prop->size == 1) { 
                        uint8_t idx = *(uint8_t*)prop->value; 
                        if (idx < doc->header.string_count && doc->strings[idx]) { 
                            free(ctx->window_title); 
                            ctx->window_title = strdup(doc->strings[idx]); 
                        } 
                    }
                    break;
                case PROP_ID_RESIZABLE:
                    if (prop->value_type == VAL_TYPE_BYTE && prop->size == 1) { 
                        ctx->resizable = *(uint8_t*)prop->value; 
                    }
                    break;
                case PROP_ID_SCALE_FACTOR:
                    if (prop->value_type == VAL_TYPE_PERCENTAGE && prop->size == 2) { 
                        uint16_t sf = krb_read_u16_le(prop->value); 
                        ctx->scale_factor = sf / 256.0f; 
                    }
                    break;
                default:
                    apply_property_to_element(app_element, prop, doc, debug_file);
                    break;
            }
        }
    }
    
    app_element->render_w = ctx->window_width; 
    app_element->render_h = ctx->window_height; 
    app_element->render_x = 0; 
    app_element->render_y = 0;
}

void apply_element_styling(RenderElement* el, KrbDocument* doc, RenderContext* ctx, FILE* debug_file) {
    if (!el || !doc || !ctx) return;
    
    // Apply Style
    if (el->header.style_id > 0 && el->header.style_id <= doc->header.style_count && doc->styles) {
        int style_idx = el->header.style_id - 1; 
        KrbStyle* style = &doc->styles[style_idx];
        for(int j = 0; j < style->property_count; j++) { 
            apply_property_to_element(el, &style->properties[j], doc, debug_file);
        }
    }
    
    // Apply Direct Properties
    if (doc->properties && el->original_index < doc->header.element_count && doc->properties[el->original_index]) {
        for (int j = 0; j < el->header.property_count; j++) { 
            apply_property_to_element(el, &doc->properties[el->original_index][j], doc, debug_file);
        }
    }
    
    // Copy custom properties if present
    if (doc->elements[el->original_index].custom_prop_count > 0 && doc->custom_properties && doc->custom_properties[el->original_index]) {
        el->custom_prop_count = doc->elements[el->original_index].custom_prop_count;
        el->custom_properties = calloc(el->custom_prop_count, sizeof(KrbCustomProperty));
        if (el->custom_properties) {
            for (uint8_t j = 0; j < el->custom_prop_count; j++) {
                el->custom_properties[j] = doc->custom_properties[el->original_index][j];
            }
        }
    }
}

void build_element_tree(RenderContext* ctx, FILE* debug_file) {
    if (!ctx || !ctx->doc) return;
    
    fprintf(debug_file, "INFO: Building element tree...\n");
    
    RenderElement* parent_stack[MAX_ELEMENTS]; 
    int stack_top = -1;

    for (int i = 0; i < ctx->original_element_count; i++) {
        RenderElement* current_el = &ctx->elements[i];
        
        // Find parent based on child count structure
        while (stack_top >= 0) { 
            RenderElement* p = parent_stack[stack_top]; 
            if (p->child_count >= p->header.child_count) 
                stack_top--; 
            else 
                break; 
        }
        
        // Set parent relationship
        if (stack_top >= 0) { 
            RenderElement* p = parent_stack[stack_top]; 
            current_el->parent = p; 
            if (p->child_count < MAX_ELEMENTS) 
                p->children[p->child_count++] = current_el; 
        }
        
        // Push to stack if has children
        if (current_el->header.child_count > 0) { 
            if (stack_top + 1 < MAX_ELEMENTS) 
                parent_stack[++stack_top] = current_el; 
        }
    }
    
    fprintf(debug_file, "INFO: Element tree built\n");
}

bool expand_all_components(RenderContext* ctx, FILE* debug_file) {
    if (!ctx || !ctx->doc) return false;
    
    fprintf(debug_file, "INFO: Expanding components...\n");
    
    // Find all component placeholders
    for (int i = 0; i < ctx->original_element_count; i++) {
        RenderElement* element = &ctx->elements[i];
        
        if (element->custom_prop_count > 0 && element->custom_properties) {
            uint8_t component_name_index;
            
            if (find_component_name_property(element->custom_properties, element->custom_prop_count,
                                           ctx->doc->strings, &component_name_index)) {
                
                // Find and expand the component
                if (!expand_component_for_element(ctx, element, component_name_index, debug_file)) {
                    fprintf(debug_file, "ERROR: Failed to expand component for element %d\n", i);
                    return false;
                }
            }
        }
    }
    
    fprintf(debug_file, "INFO: Component expansion complete\n");
    return true;
}

bool expand_component_for_element(RenderContext* ctx, RenderElement* element, uint8_t component_name_index, FILE* debug_file) {
    if (!ctx || !element || !ctx->doc) return false;
    
    // For now, mark as placeholder and create a simple component instance
    element->is_placeholder = true;
    
    // Find the component definition
    KrbComponentDefinition* comp_def = NULL;
    for (uint8_t i = 0; i < ctx->doc->header.component_def_count; i++) {
        if (ctx->doc->component_defs[i].name_index == component_name_index) {
            comp_def = &ctx->doc->component_defs[i];
            break;
        }
    }
    
    if (!comp_def) {
        fprintf(debug_file, "ERROR: Component definition not found for name index %d\n", component_name_index);
        return false;
    }
    
    // Create a simple component instance (simplified for now)
    ComponentInstance* instance = calloc(1, sizeof(ComponentInstance));
    if (!instance) return false;
    
    instance->definition_index = comp_def - ctx->doc->component_defs;
    instance->placeholder = element;
    
    // Create a simple root element for the component
    if (ctx->element_count >= MAX_ELEMENTS) {
        free(instance);
        return false;
    }
    
    RenderElement* component_root = &ctx->elements[ctx->element_count++];
    initialize_render_element(component_root, &comp_def->root_template_header, -1, ctx);
    component_root->is_component_instance = true;
    component_root->component_instance = instance;
    component_root->parent = element->parent;
    
    // Copy properties from placeholder to component root
    component_root->header.id = element->header.id;
    component_root->header.pos_x = element->header.pos_x;
    component_root->header.pos_y = element->header.pos_y;
    component_root->header.width = element->header.width;
    component_root->header.height = element->header.height;
    component_root->header.layout = element->header.layout;
    component_root->header.style_id = element->header.style_id;
    
    instance->root = component_root;
    element->children[0] = component_root;
    element->child_count = 1;
    
    // Add to context's instance list
    instance->next = ctx->instances;
    ctx->instances = instance;
    
    fprintf(debug_file, "INFO: Expanded component for element %d (component name index %d)\n", 
            element->original_index, component_name_index);
    
    return true;
}

void apply_property_inheritance(RenderContext* ctx, FILE* debug_file) {
    if (!ctx || ctx->root_count == 0) return;
    
    fprintf(debug_file, "INFO: Applying property inheritance...\n");
    
    // Start inheritance from each root
    for (int i = 0; i < ctx->root_count; i++) {
        if (ctx->roots[i]) {
            inherit_properties_recursive(ctx->roots[i], ctx, debug_file);
        }
    }
    
    fprintf(debug_file, "INFO: Property inheritance complete\n");
}
void inherit_properties_recursive(RenderElement* el, RenderContext* ctx, FILE* debug_file) {
    if (!el) {
        if (debug_file) {
            fprintf(debug_file, "WARNING: inherit_properties_recursive called with NULL element\n");
        }
        return;
    }
    
    if (debug_file) {
        fprintf(debug_file, "INHERIT: Processing element %d (type=0x%02X)\n", 
                el->original_index, el->header.type);
    }
    
    // Set defaults based on element type and parent
    Color default_fg = {255, 255, 255, 255}; // White default
    float default_font_size = BASE_FONT_SIZE;
    int default_text_alignment = 1; // Center
    
    // For text elements, be more aggressive with defaults
    if (el->header.type == ELEM_TYPE_TEXT) {
        default_fg = (Color){255, 255, 0, 255}; // Force yellow for visibility
        default_font_size = BASE_FONT_SIZE;
        default_text_alignment = 1; // Center
        
        if (debug_file) {
            fprintf(debug_file, "  TEXT ELEMENT BEFORE: fg=(%d,%d,%d,%d) font_size=%.1f align=%d\n",
                    el->fg_color.r, el->fg_color.g, el->fg_color.b, el->fg_color.a,
                    el->font_size, el->text_alignment);
        }
    }
    
    // Inherit or set foreground color
    if (el->fg_color.a == 0) {
        if (el->parent && el->parent->fg_color.a > 0) {
            el->fg_color = el->parent->fg_color;
            if (debug_file) {
                fprintf(debug_file, "  INHERITED fg_color from parent: (%d,%d,%d,%d)\n",
                        el->fg_color.r, el->fg_color.g, el->fg_color.b, el->fg_color.a);
            }
        } else {
            el->fg_color = (el->header.type == ELEM_TYPE_TEXT) ? default_fg : ctx->default_fg;
            if (debug_file) {
                fprintf(debug_file, "  SET DEFAULT fg_color: (%d,%d,%d,%d)\n",
                        el->fg_color.r, el->fg_color.g, el->fg_color.b, el->fg_color.a);
            }
        }
    } else {
        if (debug_file) {
            fprintf(debug_file, "  KEPT EXISTING fg_color: (%d,%d,%d,%d)\n",
                    el->fg_color.r, el->fg_color.g, el->fg_color.b, el->fg_color.a);
        }
    }
    
    // Inherit or set font size
    if (el->font_size <= 0.0f) {
        if (el->parent && el->parent->font_size > 0.0f) {
            el->font_size = el->parent->font_size;
            if (debug_file) {
                fprintf(debug_file, "  INHERITED font_size from parent: %.1f\n", el->font_size);
            }
        } else {
            el->font_size = default_font_size;
            if (debug_file) {
                fprintf(debug_file, "  SET DEFAULT font_size: %.1f\n", el->font_size);
            }
        }
    } else {
        if (debug_file) {
            fprintf(debug_file, "  KEPT EXISTING font_size: %.1f\n", el->font_size);
        }
    }
    
    // Inherit or set text alignment
    if (el->text_alignment == 0) {
        if (el->parent && el->parent->text_alignment > 0) {
            el->text_alignment = el->parent->text_alignment;
            if (debug_file) {
                fprintf(debug_file, "  INHERITED text_alignment from parent: %d\n", el->text_alignment);
            }
        } else {
            el->text_alignment = (el->header.type == ELEM_TYPE_TEXT) ? default_text_alignment : 0;
            if (debug_file) {
                fprintf(debug_file, "  SET DEFAULT text_alignment: %d\n", el->text_alignment);
            }
        }
    } else {
        if (debug_file) {
            fprintf(debug_file, "  KEPT EXISTING text_alignment: %d\n", el->text_alignment);
        }
    }
    
    // Special validation for text elements
    if (el->header.type == ELEM_TYPE_TEXT) {
        // Ensure minimum visible values
        if (el->fg_color.a < 50) {
            el->fg_color.a = 255;
            if (debug_file) {
                fprintf(debug_file, "  FIXED: Alpha was too low, set to 255\n");
            }
        }
        
        if (el->font_size < 8.0f) {
            el->font_size = BASE_FONT_SIZE;
            if (debug_file) {
                fprintf(debug_file, "  FIXED: Font size was too small, set to %.1f\n", el->font_size);
            }
        }
        
        if (debug_file) {
            fprintf(debug_file, "  TEXT ELEMENT FINAL: fg=(%d,%d,%d,%d) font_size=%.1f align=%d\n",
                    el->fg_color.r, el->fg_color.g, el->fg_color.b, el->fg_color.a,
                    el->font_size, el->text_alignment);
        }
    }
    
    // Validate that we have reasonable values
    if (debug_file) {
        if (el->fg_color.a == 0) {
            fprintf(debug_file, "  ERROR: Element still has transparent color after inheritance!\n");
        }
        if (el->font_size <= 0.0f) {
            fprintf(debug_file, "  ERROR: Element still has invalid font size after inheritance!\n");
        }
    }
    
    // Recursively process children
    for (int i = 0; i < el->child_count; i++) {
        if (el->children[i]) {
            inherit_properties_recursive(el->children[i], ctx, debug_file);
        } else {
            if (debug_file) {
                fprintf(debug_file, "  WARNING: Child %d is NULL\n", i);
            }
        }
    }
    
    if (debug_file) {
        fprintf(debug_file, "INHERIT: Finished processing element %d\n", el->original_index);
    }
}

void find_root_elements(RenderContext* ctx, FILE* debug_file) {
    if (!ctx) return;
    
    ctx->root_count = 0;
    
    for (int i = 0; i < ctx->element_count; i++) {
        RenderElement* el = &ctx->elements[i];
        if (!el->parent && !el->is_placeholder && ctx->root_count < MAX_ELEMENTS) {
            ctx->roots[ctx->root_count++] = el;
        }
    }
    
    fprintf(debug_file, "INFO: Found %d root elements\n", ctx->root_count);
}

void load_all_textures(RenderContext* ctx, const char* base_dir, FILE* debug_file) {
    if (!ctx || !ctx->doc) return;
    
    fprintf(debug_file, "INFO: Loading textures from base dir: %s\n", base_dir);
    
    for (int i = 0; i < ctx->element_count; i++) {
        RenderElement* el = &ctx->elements[i];
        if (el->header.type == ELEM_TYPE_IMAGE && el->resource_index != INVALID_RESOURCE_INDEX) {
            if (el->resource_index >= ctx->doc->header.resource_count || !ctx->doc->resources) { 
                continue; 
            }
            
            KrbResource* res = &ctx->doc->resources[el->resource_index];
            if (res->format == RES_FORMAT_EXTERNAL) {
                if (res->data_string_index >= ctx->doc->header.string_count || !ctx->doc->strings || !ctx->doc->strings[res->data_string_index]) { 
                    continue; 
                }
                
                const char* relative_path = ctx->doc->strings[res->data_string_index];
                char full_path[512];
                snprintf(full_path, sizeof(full_path), "%s/%s", base_dir, relative_path);
                
                el->texture = LoadTexture(full_path);
                if (IsTextureReady(el->texture)) {
                    el->texture_loaded = true;
                    fprintf(debug_file, "  Loaded texture: %s\n", full_path);
                } else {
                    fprintf(debug_file, "  Failed to load texture: %s\n", full_path);
                    el->texture_loaded = false;
                }
            }
        }
    }
}

void handle_window_resize(RenderContext* ctx) {
    if (!ctx) return;
    
    if (ctx->resizable && IsWindowResized()) { 
        ctx->window_width = GetScreenWidth(); 
        ctx->window_height = GetScreenHeight(); 
        
        // Update app element size if it exists
        if (ctx->element_count > 0 && ctx->elements[0].header.type == ELEM_TYPE_APP) {
            ctx->elements[0].render_w = ctx->window_width; 
            ctx->elements[0].render_h = ctx->window_height; 
        }
    }
}

RenderContext* create_render_context(KrbDocument* doc, FILE* debug_file) {
    if (!doc) return NULL;
    
    RenderContext* ctx = calloc(1, sizeof(RenderContext));
    if (!ctx) return NULL;
    
    ctx->doc = doc;
    ctx->original_element_count = doc->header.element_count;
    ctx->element_count = doc->header.element_count;
    ctx->instances = NULL;
    ctx->root_count = 0;
    
    // Allocate elements array with extra space for component expansion
    ctx->elements = calloc(MAX_ELEMENTS, sizeof(RenderElement));
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
    
    // Free element text strings and custom properties
    for (int i = 0; i < ctx->element_count; i++) {
        if (ctx->elements[i].text) {
            free(ctx->elements[i].text);
            ctx->elements[i].text = NULL;
        }
        
        if (ctx->elements[i].custom_properties) {
            free(ctx->elements[i].custom_properties);
            ctx->elements[i].custom_properties = NULL;
        }
    }
    
    // Free component instances
    ComponentInstance* instance = ctx->instances;
    while (instance) {
        ComponentInstance* next = instance->next;
        free(instance);
        instance = next;
    }
    
    // Free the main elements array
    free(ctx->elements);
    
    // Free window title
    if (ctx->window_title) {
        free(ctx->window_title);
    }
    
    // Free the context itself
    free(ctx);
}
void apply_contextual_defaults(RenderElement* el, RenderContext* ctx, FILE* debug_file) {
    if (!el || !ctx) return;
    
    if (debug_file) {
        fprintf(debug_file, "CONTEXTUAL DEFAULTS: Element %d (type=0x%02X)\n", 
                el->original_index, el->header.type);
    }
    
    // Check border color and width relationship (per Section 3 of spec)
    bool has_border_color = (el->border_color.a > 0);
    bool has_border_width = (el->border_widths[0] > 0 || el->border_widths[1] > 0 || 
                            el->border_widths[2] > 0 || el->border_widths[3] > 0);
    
    // Rule 1: If BorderColor is set and all BorderWidths are 0, default BorderWidths to 1
    if (has_border_color && !has_border_width) {
        memset(el->border_widths, 1, 4); // Set all sides to 1px
        if (debug_file) {
            fprintf(debug_file, "  -> Applied contextual default: border_width=1 (because border_color is set)\n");
        }
    }
    
    // Rule 2: If any BorderWidths > 0 and BorderColor is transparent, default to DefaultBorderColor
    if (has_border_width && !has_border_color) {
        el->border_color = ctx->default_border;
        if (debug_file) {
            fprintf(debug_file, "  -> Applied contextual default: border_color=default (because border_width > 0)\n");
        }
    }
    
    if (debug_file) {
        fprintf(debug_file, "  FINAL BORDER STATE: color=(%d,%d,%d,%d) widths=[%d,%d,%d,%d]\n",
                el->border_color.r, el->border_color.g, el->border_color.b, el->border_color.a,
                el->border_widths[0], el->border_widths[1], el->border_widths[2], el->border_widths[3]);
    }
}

void reset_cursor_for_frame(void) {
    g_cursor_set_this_frame = false;
    g_highest_cursor_priority = -1;
}

void render_element(RenderElement* el, int parent_content_x, int parent_content_y, int parent_content_width, int parent_content_height, float scale_factor, FILE* debug_file) {
    if (!el) return;

    // Skip rendering placeholder elements
    if (el->is_placeholder) {
        if (debug_file) {
            fprintf(debug_file, "DEBUG RENDER: Skipping placeholder element %d\n", el->original_index);
        }
        return;
    }

    // Skip rendering invisible elements
    if (!el->is_visible) {
        if (debug_file) {
            fprintf(debug_file, "DEBUG RENDER: Skipping invisible element %d\n", el->original_index);
        }
        return;
    }

    // Check if element already has pre-calculated size (from custom components)
    bool has_precalculated_size = (el->render_w > 0 && el->render_h > 0);
    
    int intrinsic_w, intrinsic_h;
    
    if (has_precalculated_size) {
        // Use pre-calculated size from custom components
        intrinsic_w = el->render_w;
        intrinsic_h = el->render_h;
        if (debug_file) {
            fprintf(debug_file, "DEBUG RENDER: Using pre-calculated size for Elem %d: %dx%d\n", 
                    el->original_index, intrinsic_w, intrinsic_h);
        }
    } else {
        // --- Calculate Intrinsic Size ---
        intrinsic_w = (int)(el->header.width * scale_factor);
        intrinsic_h = (int)(el->header.height * scale_factor);
        
        if (el->header.type == ELEM_TYPE_TEXT && el->text) {
                // CRITICAL FIX: Ensure font size is never 0
                float font_size = (el->font_size > 0) ? el->font_size : BASE_FONT_SIZE;
                int scaled_font_size = (int)(font_size * scale_factor);
                if (scaled_font_size < 1) scaled_font_size = 1;
                int text_width_measured = (el->text[0] != '\0') ? MeasureText(el->text, scaled_font_size) : 0;
                if (el->header.width == 0) intrinsic_w = text_width_measured + (int)(8 * scale_factor);
                if (el->header.height == 0) intrinsic_h = scaled_font_size + (int)(8 * scale_factor);
                
                // Debug output
                if (debug_file) {
                    fprintf(debug_file, "  TEXT SIZE CALC: font_size=%.1f scaled=%d measured=%d intrinsic=%dx%d\n",
                            font_size, scaled_font_size, text_width_measured, intrinsic_w, intrinsic_h);
                }
            }
        else if (el->header.type == ELEM_TYPE_BUTTON && el->text) {
            float font_size = (el->font_size > 0) ? el->font_size : BASE_FONT_SIZE;
            int scaled_font_size = (int)(font_size * scale_factor);
            if (scaled_font_size < 1) scaled_font_size = 1;
            int text_width_measured = (el->text[0] != '\0') ? MeasureText(el->text, scaled_font_size) : 0;
            if (el->header.width == 0) intrinsic_w = text_width_measured + (int)(16 * scale_factor);
            if (el->header.height == 0) intrinsic_h = scaled_font_size + (int)(16 * scale_factor);
        }
        else if (el->header.type == ELEM_TYPE_IMAGE && el->texture_loaded) {
            if (el->header.width == 0) intrinsic_w = (int)(el->texture.width * scale_factor);
            if (el->header.height == 0) intrinsic_h = (int)(el->texture.height * scale_factor);
        }

        // Clamp minimum size
        if (intrinsic_w < 0) intrinsic_w = 0;
        if (intrinsic_h < 0) intrinsic_h = 0;
        if (el->header.width > 0 && intrinsic_w == 0) intrinsic_w = 1;
        if (el->header.height > 0 && intrinsic_h == 0) intrinsic_h = 1;
    }

    // --- Determine Final Position & Size (Layout) ---
    int final_x, final_y;
    int final_w = intrinsic_w;
    int final_h = intrinsic_h;
    bool has_pos = (el->header.pos_x != 0 || el->header.pos_y != 0);
    bool is_absolute = (el->header.layout & LAYOUT_ABSOLUTE_BIT);

    // Position calculation
    if (has_precalculated_size && el->render_x != 0 && el->render_y != 0) {
        // Use pre-calculated position from custom components
        final_x = el->render_x;
        final_y = el->render_y;
    } else if (is_absolute || has_pos) {
        // Absolute positioning
        final_x = parent_content_x + (int)(el->header.pos_x * scale_factor);
        final_y = parent_content_y + (int)(el->header.pos_y * scale_factor);
    } else if (el->parent != NULL) {
        // Flow layout - position determined by parent's layout logic
        final_x = el->render_x;
        final_y = el->render_y;
    } else {
        // Root element - defaults to parent content origin
        final_x = parent_content_x;
        final_y = parent_content_y;
    }

    // Store final calculated render coordinates
    el->render_x = final_x;
    el->render_y = final_y;
    el->render_w = final_w;
    el->render_h = final_h;

    // --- Check for mouse hover (for interactive elements) ---
    bool is_hovered = false;
    if (el->is_interactive) {
        Vector2 mouse_pos = GetMousePosition();
        is_hovered = (mouse_pos.x >= el->render_x && mouse_pos.x < el->render_x + el->render_w &&
                     mouse_pos.y >= el->render_y && mouse_pos.y < el->render_y + el->render_h);
        
        // Set cursor for interactive elements with priority system
        if (is_hovered) {
            int cursor_priority = 100; // Interactive elements get high priority
            
            // Only set cursor if this element has higher or equal priority to current
            if (!g_cursor_set_this_frame || cursor_priority >= g_highest_cursor_priority) {
                SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
                g_cursor_set_this_frame = true;
                g_highest_cursor_priority = cursor_priority;
            }
        }
    }

    // --- Apply Styling (with hover effects) ---
    Color bg_color = el->bg_color;
    Color fg_color = el->fg_color;
    Color border_color = el->border_color;
    
    // Apply hover effects for buttons
    if (el->header.type == ELEM_TYPE_BUTTON && is_hovered) {
        // Brighten background on hover
        bg_color.r = (bg_color.r < 200) ? bg_color.r + 55 : 255;
        bg_color.g = (bg_color.g < 200) ? bg_color.g + 55 : 255;
        bg_color.b = (bg_color.b < 200) ? bg_color.b + 55 : 255;
        
        // Brighten border on hover
        border_color.r = (border_color.r < 200) ? border_color.r + 55 : 255;
        border_color.g = (border_color.g < 200) ? border_color.g + 255 : 255;
        border_color.b = (border_color.b < 200) ? border_color.b + 55 : 255;
    }

    int top_bw = (int)(el->border_widths[0] * scale_factor);
    int right_bw = (int)(el->border_widths[1] * scale_factor);
    int bottom_bw = (int)(el->border_widths[2] * scale_factor);
    int left_bw = (int)(el->border_widths[3] * scale_factor);

    // Clamp borders if they exceed element size
    if (el->render_h > 0 && top_bw + bottom_bw >= el->render_h) { 
        top_bw = el->render_h > 1 ? 1 : el->render_h; 
        bottom_bw = 0; 
    }
    if (el->render_w > 0 && left_bw + right_bw >= el->render_w) { 
        left_bw = el->render_w > 1 ? 1 : el->render_w; 
        right_bw = 0; 
    }

    // Debug Logging
    if (debug_file) {
        fprintf(debug_file, "DEBUG RENDER: Elem %d (Type=0x%02X) @(%d,%d) Size=%dx%d Borders=[%d,%d,%d,%d] Layout=0x%02X ResIdx=%d Visible=%s Hovered=%s\n",
                el->original_index, el->header.type, el->render_x, el->render_y, el->render_w, el->render_h,
                top_bw, right_bw, bottom_bw, left_bw, el->header.layout, el->resource_index,
                el->is_visible ? "true" : "false", is_hovered ? "true" : "false");
    }

    // --- Draw Background ---
    bool draw_background = (el->header.type != ELEM_TYPE_TEXT);
    if (draw_background && el->render_w > 0 && el->render_h > 0 && bg_color.a > 0) {
        DrawRectangle(el->render_x, el->render_y, el->render_w, el->render_h, bg_color);
    }

    // --- Draw Borders ---
    if (el->render_w > 0 && el->render_h > 0 && border_color.a > 0) {
        if (top_bw > 0) DrawRectangle(el->render_x, el->render_y, el->render_w, top_bw, border_color);
        if (bottom_bw > 0) DrawRectangle(el->render_x, el->render_y + el->render_h - bottom_bw, el->render_w, bottom_bw, border_color);
        int side_border_y = el->render_y + top_bw;
        int side_border_height = el->render_h - top_bw - bottom_bw; 
        if (side_border_height < 0) side_border_height = 0;
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
            float font_size = (el->font_size > 0) ? el->font_size : BASE_FONT_SIZE;
            int scaled_font_size = (int)(font_size * scale_factor);
            if (scaled_font_size < 1) scaled_font_size = 1;
            
            int text_width_measured = MeasureText(el->text, scaled_font_size);
            int text_draw_x = content_x;
            if (el->text_alignment == 1) text_draw_x = content_x + (content_width - text_width_measured) / 2; // Center
            else if (el->text_alignment == 2) text_draw_x = content_x + content_width - text_width_measured;   // End/Right
            int text_draw_y = content_y + (content_height - scaled_font_size) / 2; // Vertical center

            if (text_draw_x < content_x) text_draw_x = content_x; // Clamp
            if (text_draw_y < content_y) text_draw_y = content_y; // Clamp

            // Force visible color for text if it's transparent or invisible
            if (fg_color.a == 0 || (fg_color.r == 0 && fg_color.g == 0 && fg_color.b == 0)) {
                fg_color = (Color){255, 255, 255, 255}; // Force white
            }

            if (debug_file) fprintf(debug_file, "  -> Drawing Text (Type %02X) '%s' (align=%d) with color (%d,%d,%d,%d) at (%d,%d) font_size=%d within content (%d,%d %dx%d)\n", 
                                   el->header.type, el->text, el->text_alignment, fg_color.r, fg_color.g, fg_color.b, fg_color.a,
                                   text_draw_x, text_draw_y, scaled_font_size, content_x, content_y, content_width, content_height);
            DrawText(el->text, text_draw_x, text_draw_y, scaled_font_size, fg_color);
        }
        
        // Draw Image
        else if (el->header.type == ELEM_TYPE_IMAGE && el->texture_loaded) {
             if (debug_file) fprintf(debug_file, "  -> Drawing Image Texture (ResIdx %d) within content (%d,%d %dx%d)\n", 
                                    el->resource_index, content_x, content_y, content_width, content_height);
             Rectangle sourceRec = { 0.0f, 0.0f, (float)el->texture.width, (float)el->texture.height };
             Rectangle destRec = { (float)content_x, (float)content_y, (float)content_width, (float)content_height };
             Vector2 origin = { 0.0f, 0.0f };
             DrawTexturePro(el->texture, sourceRec, destRec, origin, 0.0f, WHITE);
        }

        EndScissorMode();
    }

    // --- Handle Click Events ---
    if (el->header.type == ELEM_TYPE_BUTTON && is_hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (debug_file) {
            fprintf(debug_file, "BUTTON CLICKED: Element %d\n", el->original_index);
        }
        // You can add onClick handler logic here
    }

    // [Rest of the function remains the same - layout and render children section]
    if (el->child_count > 0 && content_width > 0 && content_height > 0) {
        uint8_t direction = el->header.layout & LAYOUT_DIRECTION_MASK;
        uint8_t alignment = (el->header.layout & LAYOUT_ALIGNMENT_MASK) >> 2;
        int current_flow_x = content_x;
        int current_flow_y = content_y;
        int total_child_width_scaled = 0;
        int total_child_height_scaled = 0;
        int flow_child_count = 0;
        int child_sizes[MAX_ELEMENTS][2];

        if (debug_file) fprintf(debug_file, "  Layout Children of Elem %d: Count=%d Dir=%d Align=%d Content=(%d,%d %dx%d)\n", 
                               el->original_index, el->child_count, direction, alignment, content_x, content_y, content_width, content_height);

        // Pass 1: Calculate sizes and total dimensions of flow children
        for (int i = 0; i < el->child_count; i++) {
            RenderElement* child = el->children[i];
            if (!child) continue;
            
            if (child->is_placeholder || !child->is_visible) {
                child_sizes[i][0] = 0; 
                child_sizes[i][1] = 0;
                continue;
            }
            
            bool child_is_absolute = (child->header.layout & LAYOUT_ABSOLUTE_BIT);
            bool child_has_pos = (child->header.pos_x != 0 || child->header.pos_y != 0);

            if (child_is_absolute || child_has_pos) {
                child_sizes[i][0] = 0; 
                child_sizes[i][1] = 0;
                continue;
            }

            // Use pre-calculated size if available, otherwise calculate
            int child_w, child_h;
            if (child->render_w > 0 && child->render_h > 0) {
                child_w = child->render_w;
                child_h = child->render_h;
            } else {
                child_w = (int)(child->header.width * scale_factor);
                child_h = (int)(child->header.height * scale_factor);
                
                if (child->header.type == ELEM_TYPE_TEXT && child->text) {
                    float font_size = (child->font_size > 0) ? child->font_size : BASE_FONT_SIZE;
                    int fs = (int)(font_size * scale_factor); if(fs<1)fs=1;
                    int tw = (child->text[0]!='\0') ? MeasureText(child->text, fs):0;
                    if (child->header.width == 0) child_w = tw + (int)(8 * scale_factor);
                    if (child->header.height == 0) child_h = fs + (int)(8 * scale_factor);
                }
                else if (child->header.type == ELEM_TYPE_BUTTON && child->text) {
                    float font_size = (child->font_size > 0) ? child->font_size : BASE_FONT_SIZE;
                    int fs = (int)(font_size * scale_factor); if(fs<1)fs=1;
                    int tw = (child->text[0]!='\0') ? MeasureText(child->text, fs):0;
                    if (child->header.width == 0) child_w = tw + (int)(16 * scale_factor);
                    if (child->header.height == 0) child_h = fs + (int)(16 * scale_factor);
                }
                else if (child->header.type == ELEM_TYPE_IMAGE && child->texture_loaded) {
                    if (child->header.width == 0) child_w = (int)(child->texture.width * scale_factor);
                    if (child->header.height == 0) child_h = (int)(child->texture.height * scale_factor);
                }
            }

            if (child_w < 0) child_w = 0;
            if (child_h < 0) child_h = 0;
            if (child->header.width > 0 && child_w == 0) child_w = 1;
            if (child->header.height > 0 && child_h == 0) child_h = 1;

            child_sizes[i][0] = child_w;
            child_sizes[i][1] = child_h;

            if (direction == 0x00 || direction == 0x02) {
                total_child_width_scaled += child_w;
            } else {
                total_child_height_scaled += child_h;
            }
            flow_child_count++;
        }

        // Pass 2: Calculate starting position based on alignment
        if (direction == 0x00 || direction == 0x02) {
            if (alignment == 0x01) { 
                current_flow_x = content_x + (content_width - total_child_width_scaled) / 2; 
            } else if (alignment == 0x02) { 
                current_flow_x = content_x + content_width - total_child_width_scaled; 
            }
            if (current_flow_x < content_x) current_flow_x = content_x;
        } else {
            if (alignment == 0x01) { 
                current_flow_y = content_y + (content_height - total_child_height_scaled) / 2; 
            } else if (alignment == 0x02) { 
                current_flow_y = content_y + content_height - total_child_height_scaled; 
            }
            if (current_flow_y < content_y) current_flow_y = content_y;
        }

        // Calculate spacing for SpaceBetween
        float space_between = 0;
        if (alignment == 0x03 && flow_child_count > 1) {
            if (direction == 0x00 || direction == 0x02) {
                space_between = (float)(content_width - total_child_width_scaled) / (flow_child_count - 1);
            } else {
                space_between = (float)(content_height - total_child_height_scaled) / (flow_child_count - 1);
            }
            if (space_between < 0) space_between = 0;
        }

        // Pass 3: Position and render children
        int flow_children_processed = 0;
        for (int i = 0; i < el->child_count; i++) {
            RenderElement* child = el->children[i];
            if (!child) continue;
            
            if (child->is_placeholder || !child->is_visible) continue;

            bool child_is_absolute = (child->header.layout & LAYOUT_ABSOLUTE_BIT);
            bool child_has_pos = (child->header.pos_x != 0 || child->header.pos_y != 0);

            if (child_is_absolute || child_has_pos) {
                render_element(child, content_x, content_y, content_width, content_height, scale_factor, debug_file);
            } else {
                int child_w = child_sizes[i][0];
                int child_h = child_sizes[i][1];
                int child_final_x, child_final_y;

                if (direction == 0x00 || direction == 0x02) {
                    child_final_x = current_flow_x;
                    if (alignment == 0x01) child_final_y = content_y + (content_height - child_h) / 2;
                    else if (alignment == 0x02) child_final_y = content_y + content_height - child_h;
                    else child_final_y = content_y;
                } else {
                    child_final_y = current_flow_y;
                    if (alignment == 0x01) child_final_x = content_x + (content_width - child_w) / 2;
                    else if (alignment == 0x02) child_final_x = content_x + content_width - child_w;
                    else child_final_x = content_x;
                }

                child->render_x = child_final_x;
                child->render_y = child_final_y;

                render_element(child, content_x, content_y, content_width, content_height, scale_factor, debug_file);

                if (direction == 0x00 || direction == 0x02) {
                    current_flow_x += child_w;
                    if (alignment == 0x03 && flow_children_processed < flow_child_count - 1) {
                        current_flow_x += (int)roundf(space_between);
                    }
                } else {
                    current_flow_y += child_h;
                    if (alignment == 0x03 && flow_children_processed < flow_child_count - 1) {
                        current_flow_y += (int)roundf(space_between);
                    }
                }
                flow_children_processed++;
            }
        }
    }

    if (debug_file) fprintf(debug_file, "  Finished Render Elem %d\n", el->original_index);
}

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
    
    // --- Initialize Custom Components System ---
    init_custom_components();

    // --- Read KRB Document ---
    FILE* file = fopen(krb_file_path, "rb");
    if (!file) { 
        fprintf(stderr, "ERROR: Cannot open '%s': %s\n", krb_file_path, strerror(errno)); 
        free(krb_file_path_copy); 
        if (debug_file != stderr) fclose(debug_file); 
        return 1; 
    }

    KrbDocument doc = {0};
    if (!krb_read_document(file, &doc)) {
        fprintf(stderr, "ERROR: Failed parse KRB '%s'\n", krb_file_path);
        fclose(file); krb_free_document(&doc); free(krb_file_path_copy); 
        if (debug_file != stderr) fclose(debug_file);
        return 1;
    }
    fclose(file);

    // --- Create Render Context ---
    RenderContext* ctx = create_render_context(&doc, debug_file);
    if (!ctx) {
        fprintf(stderr, "ERROR: Failed to create render context\n");
        krb_free_document(&doc); free(krb_file_path_copy);
        if (debug_file != stderr) fclose(debug_file);
        return 1;
    }

    // --- Process App Element ---
    RenderElement* app_element = NULL;
    if ((doc.header.flags & FLAG_HAS_APP) && doc.header.element_count > 0 && doc.elements[0].type == ELEM_TYPE_APP) {
        app_element = &ctx->elements[0];
        // Initialize App element
        initialize_render_element(app_element, &doc.elements[0], 0, ctx);
        app_element->is_visible = true; // App is always visible
        
        // Apply App properties for window config
        process_app_element_properties(app_element, &doc, ctx, debug_file);
        
        fprintf(debug_file, "INFO: Processed App. Window:%dx%d Title:'%s' Scale:%.2f\n", 
                ctx->window_width, ctx->window_height, 
                ctx->window_title ? ctx->window_title : "(None)", ctx->scale_factor);
    }

    // --- Initialize All Elements ---
    for (int i = 0; i < doc.header.element_count; i++) {
        if (app_element && i == 0) continue; // Skip app, already processed
        
        RenderElement* el = &ctx->elements[i];
        initialize_render_element(el, &doc.elements[i], i, ctx);
        
        // Step 2 & 3: Apply styles and properties (existing code)
        apply_element_styling(el, &doc, ctx, debug_file);
        
        // Step 4: Apply contextual defaults (NEW - per your spec!)
        apply_contextual_defaults(el, ctx, debug_file);
        
        fprintf(debug_file, "INFO: Initialized Elem %d. Text='%s' Visible=%s\n", 
                i, el->text ? el->text : "NULL", el->is_visible ? "true" : "false");
    }

    // Step 5: Apply Property Inheritance (existing code)
    apply_property_inheritance(ctx, debug_file);


    // --- Build Initial Parent/Child Tree ---
    build_element_tree(ctx, debug_file);

    // --- Expand Components and Apply Inheritance ---
    if (!expand_all_components(ctx, debug_file)) {
        fprintf(stderr, "ERROR: Failed to expand components\n");
        free_render_context(ctx);
        krb_free_document(&doc); free(krb_file_path_copy);
        if (debug_file != stderr) fclose(debug_file);
        return 1;
    }

    // --- Apply Property Inheritance ---
    apply_property_inheritance(ctx, debug_file);

    // --- Process Custom Components ---
    if (!process_custom_components(ctx, debug_file)) {
        fprintf(stderr, "ERROR: Failed to process custom components\n");
        free_render_context(ctx);
        krb_free_document(&doc); free(krb_file_path_copy);
        if (debug_file != stderr) fclose(debug_file);
        return 1;
    }
    
    // --- Find Roots ---
    find_root_elements(ctx, debug_file);
    // --- Initialize Raylib ---
    InitWindow(ctx->window_width, ctx->window_height, 
        ctx->window_title ? ctx->window_title : "KRB Renderer");
    if (ctx->resizable) SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetTargetFPS(60);

    // --- Calculate Element Sizes (NOW that Raylib is initialized) ---
    fprintf(debug_file, "INFO: Calculating element sizes after Raylib initialization...\n");
    for (int i = 0; i < ctx->element_count; i++) {
    RenderElement* el = &ctx->elements[i];
    fprintf(debug_file, "CALCULATING SIZE FOR ELEMENT %d (type=0x%02X) text='%s'\n", 
        i, el->header.type, el->text ? el->text : "NULL");
    calculate_element_minimum_size(el, ctx->scale_factor);
    fprintf(debug_file, "  -> Final size: %dx%d\n", el->render_w, el->render_h);
    }

    // --- Load Textures ---
    load_all_textures(ctx, krb_dir, debug_file);
    
    // --- Main Loop ---
    while (!WindowShouldClose()) {
        handle_window_resize(ctx);
        
        // Reset cursor tracking at start of each frame
        reset_cursor_for_frame();
        
        BeginDrawing();
        Color clear_color = (app_element) ? app_element->bg_color : BLACK; 
        ClearBackground(clear_color);
        
        // Render all root elements
        for (int i = 0; i < ctx->root_count; i++) {
            if (ctx->roots[i]) {
                render_element(ctx->roots[i], 0, 0, ctx->window_width, ctx->window_height, ctx->scale_factor, debug_file); 
            }
        }
        
        // If no interactive element was hovered, ensure default cursor
        if (!g_cursor_set_this_frame) {
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        }
        
        EndDrawing();
    }
    // --- Cleanup ---
    CloseWindow();
    free_render_context(ctx);
    krb_free_document(&doc);
    free(krb_file_path_copy);
    if (debug_file != stderr) fclose(debug_file);
    
    return 0;
}

#endif // BUILD_STANDALONE_RENDERER