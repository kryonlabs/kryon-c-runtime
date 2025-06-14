#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <math.h>

// Include the renderer header and custom components
#include "renderer.h" // Includes krb.h, raylib.h, RenderElement, render_element()
#include "custom_components.h"
#include "custom_tabbar.h"

// --->>> INCLUDE THE GENERATED HEADER WITH EMBEDDED DATA <<<---
#include "tab_bar_krb_data.h" // Provides get_embedded_krb_data() and _len()

// --- Add Default Definitions ---
#define DEFAULT_WINDOW_WIDTH 360
#define DEFAULT_WINDOW_HEIGHT 480
#define DEFAULT_SCALE_FACTOR 1.0f
// --- End Default Definitions ---

// --- Tab Management State ---
typedef enum {
    TAB_HOME = 0,
    TAB_SEARCH = 1,
    TAB_PROFILE = 2
} ActiveTab;

static ActiveTab current_tab = TAB_HOME;

// --- Event Handling Logic ---
void showHomePage() {
    printf(">>> Switching to HOME tab <<<\n");
    current_tab = TAB_HOME;
}

void showSearchPage() {
    printf(">>> Switching to SEARCH tab <<<\n");
    current_tab = TAB_SEARCH;
}

void showProfilePage() {
    printf(">>> Switching to PROFILE tab <<<\n");
    current_tab = TAB_PROFILE;
}

typedef void (*KrbEventHandlerFunc)();
typedef struct { const char* name; KrbEventHandlerFunc func; } EventHandlerMapping;

EventHandlerMapping event_handlers[] = {
    { "showHomePage", showHomePage },
    { "showSearchPage", showSearchPage },
    { "showProfilePage", showProfilePage },
    { NULL, NULL }
};

KrbEventHandlerFunc find_handler(const char* name) {
    if (!name) return NULL;
    for (int i = 0; event_handlers[i].name != NULL; i++) {
        if (strcmp(event_handlers[i].name, name) == 0) return event_handlers[i].func;
    }
    fprintf(stderr, "Warning: Handler function not found for name: %s\n", name);
    return NULL;
}
// --- Tab Visibility Management ---
void update_tab_visibility(RenderContext* ctx) {
    if (!ctx) return;
    
    // Find the page elements by their IDs and update visibility
    for (int i = 0; i < ctx->element_count; i++) {
        RenderElement* el = &ctx->elements[i];
        if (!el || el->is_placeholder) continue;
        
        // Get element ID string
        const char* element_id = NULL;
        if (el->header.id > 0 && el->header.id < ctx->doc->header.string_count && ctx->doc->strings[el->header.id]) {
            element_id = ctx->doc->strings[el->header.id];
        }
        
        if (element_id) {
            // Update visibility based on current tab
            if (strcmp(element_id, "page_home") == 0) {
                el->is_visible = (current_tab == TAB_HOME);
            } else if (strcmp(element_id, "page_search") == 0) {
                el->is_visible = (current_tab == TAB_SEARCH);
            } else if (strcmp(element_id, "page_profile") == 0) {
                el->is_visible = (current_tab == TAB_PROFILE);
            }
        }
    }
    
    // Update TabBar button styles to show active state
    for (int i = 0; i < ctx->element_count; i++) {
        RenderElement* el = &ctx->elements[i];
        if (!el || el->is_placeholder || el->header.type != ELEM_TYPE_BUTTON) continue;
        
        const char* element_id = NULL;
        if (el->header.id > 0 && el->header.id < ctx->doc->header.string_count && ctx->doc->strings[el->header.id]) {
            element_id = ctx->doc->strings[el->header.id];
        }
        
        if (element_id) {
            // Determine which style to use based on active tab
            uint8_t new_style_id = 0;
            bool is_active = false;
            
            if (strcmp(element_id, "tab_home") == 0 && current_tab == TAB_HOME) {
                is_active = true;
            } else if (strcmp(element_id, "tab_search") == 0 && current_tab == TAB_SEARCH) {
                is_active = true;
            } else if (strcmp(element_id, "tab_profile") == 0 && current_tab == TAB_PROFILE) {
                is_active = true;
            }
            
            // Set appropriate style ID
            if (is_active) {
                new_style_id = 4; // tab_item_style_active_base
            } else {
                new_style_id = 3; // tab_item_style_base
            }
            
            // Update style ID and re-apply style if it changed
            if (el->header.style_id != new_style_id) {
                el->header.style_id = new_style_id;
                
                // Re-apply the style properties
                if (new_style_id > 0 && new_style_id <= ctx->doc->header.style_count && ctx->doc->styles) {
                    int style_idx = new_style_id - 1;
                    KrbStyle* style = &ctx->doc->styles[style_idx];
                    
                    // Reset colors to defaults first
                    el->bg_color = ctx->default_bg;
                    el->fg_color = ctx->default_fg;
                    el->border_color = ctx->default_border;
                    
                    // Apply new style properties
                    for (int j = 0; j < style->property_count; j++) {
                        apply_property_to_element(el, &style->properties[j], ctx->doc, NULL);
                    }
                }
            }
        }
    }
}
// --- Main Application ---
int main(int argc, char* argv[]) {
    // --- Setup ---
    (void)argc; (void)argv; // Suppress unused parameter warnings

    FILE* debug_file = fopen("krb_render_debug_tabbar.log", "w");
    if (!debug_file) {
        debug_file = stderr;
        fprintf(stderr, "Warning: Could not open krb_render_debug_tabbar.log, writing debug to stderr.\n");
    }

    // --- Access Embedded KRB Data ---
    unsigned char *krb_data_buffer = get_embedded_krb_data();
    unsigned int krb_data_size = get_embedded_krb_data_len();

    fprintf(debug_file, "INFO: Using embedded TabBar KRB data (Size: %u bytes)\n", krb_data_size);

    // --- Create In-Memory Stream ---
    FILE* file = fmemopen(krb_data_buffer, krb_data_size, "rb");
    if (!file) {
        perror("ERROR: Could not create in-memory stream with fmemopen");
        if (debug_file != stderr) fclose(debug_file);
        return 1;
    }

    // --- Parsing ---
    KrbDocument doc = {0};
    fprintf(debug_file, "INFO: Reading TabBar KRB document from memory...\n");
    if (!krb_read_document(file, &doc)) {
        fprintf(stderr, "ERROR: Failed to parse embedded TabBar KRB data\n");
        fclose(file);
        krb_free_document(&doc);
        if (debug_file != stderr) fclose(debug_file);
        return 1;
    }
    fclose(file);
    
    fprintf(debug_file, "INFO: Parsed embedded TabBar KRB OK - Ver=%u.%u Elements=%u ComponentDefs=%u Styles=%u Strings=%u\n",
            doc.version_major, doc.version_minor, doc.header.element_count, 
            doc.header.component_def_count, doc.header.style_count, doc.header.string_count);

    if (doc.header.element_count == 0) {
        fprintf(stderr, "ERROR: No elements found in KRB data.\n");
        krb_free_document(&doc);
        if(debug_file!=stderr) fclose(debug_file);
        return 0;
    }

    // --- Initialize Custom Components System ---
    init_custom_components();
    fprintf(debug_file, "INFO: Initialized custom components system\n");

    // --- Create Render Context ---
    RenderContext* ctx = create_render_context(&doc, debug_file);
    if (!ctx) {
        fprintf(stderr, "ERROR: Failed to create render context\n");
        krb_free_document(&doc);
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
        app_element->is_placeholder = false;
        app_element->is_component_instance = false;
        app_element->component_instance = NULL;
        app_element->custom_properties = NULL;
        app_element->custom_prop_count = 0;
        app_element->is_visible = true; // App is always visible
        
        for(int k=0; k<MAX_ELEMENTS; ++k) app_element->children[k] = NULL;
        app_element->is_interactive = false;
        
        fprintf(debug_file, "INFO: Processing App Element (Index 0)\n");

        // Apply App Style
        if (app_element->header.style_id > 0 && app_element->header.style_id <= doc.header.style_count) {
             int style_idx = app_element->header.style_id - 1;
             if (doc.styles && style_idx >= 0) {
                KrbStyle* app_style = &doc.styles[style_idx];
                for(int j=0; j<app_style->property_count; ++j) {
                    apply_property_to_element(app_element, &app_style->properties[j], &doc, debug_file);
                }
             }
        }
        
        app_element->bg_color = ctx->default_bg;
        app_element->fg_color = ctx->default_fg;
        app_element->border_color = ctx->default_border;
        memset(app_element->border_widths, 0, 4);

        // Apply App direct properties
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

        fprintf(debug_file, "INFO: Processed App Element. Window: %dx%d, Title: '%s'\n", 
                ctx->window_width, ctx->window_height, ctx->window_title ? ctx->window_title : "(None)");

    } else {
        fprintf(debug_file, "WARN: No App element found. Using default window settings.\n");
        ctx->window_title = strdup("KRB TabBar Example");
    }

    // --- Populate & Process Remaining RenderElements ---
    for (int i = 0; i < doc.header.element_count; i++) {
        if (app_element && i == 0) continue; // Skip App element

        RenderElement* current_render_el = &ctx->elements[i];
        current_render_el->header = doc.elements[i];
        current_render_el->original_index = i;

        // Init with defaults
        current_render_el->text = NULL;
        current_render_el->bg_color = ctx->default_bg;
        current_render_el->fg_color = ctx->default_fg;
        current_render_el->border_color = ctx->default_border;
        memset(current_render_el->border_widths, 0, 4);
        current_render_el->text_alignment = 0;
        current_render_el->parent = NULL;
        current_render_el->child_count = 0;
        current_render_el->is_placeholder = false;
        current_render_el->is_component_instance = false;
        current_render_el->component_instance = NULL;
        current_render_el->custom_properties = NULL;
        current_render_el->custom_prop_count = 0;
        current_render_el->is_visible = true; // Default to visible
        
        for(int k=0; k<MAX_ELEMENTS; ++k) current_render_el->children[k] = NULL;
        current_render_el->render_x = 0;
        current_render_el->render_y = 0;
        current_render_el->render_w = 0;
        current_render_el->render_h = 0;

        // Set interactivity
        current_render_el->is_interactive = (current_render_el->header.type == ELEM_TYPE_BUTTON);

        // Copy custom properties if present
        if (doc.elements[i].custom_prop_count > 0 && doc.custom_properties && doc.custom_properties[i]) {
            current_render_el->custom_prop_count = doc.elements[i].custom_prop_count;
            current_render_el->custom_properties = calloc(current_render_el->custom_prop_count, sizeof(KrbCustomProperty));
            if (current_render_el->custom_properties) {
                for (uint8_t j = 0; j < current_render_el->custom_prop_count; j++) {
                    current_render_el->custom_properties[j] = doc.custom_properties[i][j];
                }
            }
        }

        // Apply Style FIRST
        if (current_render_el->header.style_id > 0 && current_render_el->header.style_id <= doc.header.style_count) {
            int style_idx = current_render_el->header.style_id - 1;
             if (doc.styles && style_idx >= 0) {
                 KrbStyle* style = &doc.styles[style_idx];
                 for(int j=0; j<style->property_count; ++j) {
                     apply_property_to_element(current_render_el, &style->properties[j], &doc, debug_file);
                 }
             }
        }

        // Apply Direct Properties SECOND
        if (doc.properties && i < doc.header.element_count && doc.properties[i]) {
             for (int j = 0; j < current_render_el->header.property_count; j++) {
                 apply_property_to_element(current_render_el, &doc.properties[i][j], &doc, debug_file);
            }
        }
    }

    // --- Calculate minimum sizes for all elements ---
    fprintf(debug_file, "INFO: Calculating element minimum sizes...\n");
    for (int i = 0; i < ctx->element_count; i++) {
        calculate_element_minimum_size(&ctx->elements[i], ctx->scale_factor);
    }

    // --- Process Component Instances ---
    if (!process_component_instances(ctx, debug_file)) {
        fprintf(stderr, "ERROR: Failed to process component instances\n");
        free_render_context(ctx);
        krb_free_document(&doc);
        if (debug_file != stderr) fclose(debug_file);
        return 1;
    }

    // --- Connect component children ---
    connect_component_children(ctx, debug_file);

    // --- Build Parent/Child Tree ---
    fprintf(debug_file, "INFO: Building element tree...\n");
    RenderElement* parent_stack[MAX_ELEMENTS]; 
    int stack_top = -1;
    
    for (int i = 0; i < doc.header.element_count; i++) {
        RenderElement* current_el = &ctx->elements[i];
        
        if (current_el->is_placeholder) continue;
        
        while (stack_top >= 0) {
            RenderElement* p = parent_stack[stack_top];
            if (p->child_count >= p->header.child_count) {
                stack_top--;
            } else {
                break;
            }
        }
        
        if (stack_top >= 0) {
            RenderElement* cp = parent_stack[stack_top];
            current_el->parent = cp;
            if (cp->child_count < MAX_ELEMENTS) {
                cp->children[cp->child_count++] = current_el;
            }
        }
        
        if (current_el->header.child_count > 0) {
            if (stack_top + 1 < MAX_ELEMENTS) {
                parent_stack[++stack_top] = current_el;
            }
        }
    }

    // Add component instance roots to the tree
    ComponentInstance* instance = ctx->instances;
    while (instance) {
        if (instance->root && instance->placeholder) {
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
    
    // --- Process Custom Components ---
    if (!process_custom_components(ctx, debug_file)) {
        fprintf(stderr, "ERROR: Failed to process custom components\n");
        free_render_context(ctx);
        krb_free_document(&doc);
        if (debug_file != stderr) fclose(debug_file);
        return 1;
    }

    // --- Find Roots ---
    RenderElement* root_elements[MAX_ELEMENTS]; 
    int root_count = 0;
    
    for(int i = 0; i < doc.header.element_count; ++i) {
        if (!ctx->elements[i].parent && !ctx->elements[i].is_placeholder) {
            if (root_count < MAX_ELEMENTS) {
                root_elements[root_count++] = &ctx->elements[i];
            }
        }
    }
    
    // Add component instance roots
    instance = ctx->instances;
    while (instance && root_count < MAX_ELEMENTS) {
        if (instance->root && !instance->root->parent) {
            root_elements[root_count++] = instance->root;
        }
        instance = instance->next;
    }
    
    if (root_count == 0 && doc.header.element_count > 0) {
        fprintf(stderr, "ERROR: No root element found in KRB.\n");
        free_render_context(ctx);
        krb_free_document(&doc);
        if(debug_file!=stderr) fclose(debug_file);
        return 1;
    }
    
    fprintf(debug_file, "INFO: Found %d root element(s).\n", root_count);

    // --- Set initial tab visibility ---
    update_tab_visibility(ctx);

    // --- Init Raylib Window ---
    InitWindow(ctx->window_width, ctx->window_height, ctx->window_title ? ctx->window_title : "KRB TabBar Example");
    if (ctx->resizable) SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetTargetFPS(60);
    fprintf(debug_file, "INFO: Entering main loop...\n");

    // --- Main Loop ---
    while (!WindowShouldClose()) {
        Vector2 mousePos = GetMousePosition();
        bool mouse_clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

        // --- Window Resizing ---
         if (ctx->resizable && IsWindowResized()) {
            ctx->window_width = GetScreenWidth(); 
            ctx->window_height = GetScreenHeight();
            if (app_element) { 
                app_element->render_w = ctx->window_width; 
                app_element->render_h = ctx->window_height; 
            }
             fprintf(debug_file, "INFO: Window resized to %dx%d\n", ctx->window_width, ctx->window_height);
        }

        // --- Interaction Check & Callback Execution ---
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        for (int i = doc.header.element_count - 1; i >= 0; --i) {
             RenderElement* el = &ctx->elements[i];
             if (el->is_placeholder || !el->is_visible) continue;
             
             if (el->is_interactive && el->render_w > 0 && el->render_h > 0) {
                Rectangle elementRect = { (float)el->render_x, (float)el->render_y, (float)el->render_w, (float)el->render_h };
                if (CheckCollisionPointRec(mousePos, elementRect)) {
                    SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
                    if (mouse_clicked) {
                         int original_idx = el->original_index;
                         if (doc.events && original_idx < doc.header.element_count && doc.events[original_idx]) {
                            for (int k = 0; k < doc.elements[original_idx].event_count; k++) {
                                KrbEventFileEntry* event = &doc.events[original_idx][k];
                                if (event->event_type == EVENT_TYPE_CLICK) {
                                    uint8_t callback_idx = event->callback_id;
                                    if (callback_idx < doc.header.string_count && doc.strings[callback_idx]) {
                                        const char* handler_name = doc.strings[callback_idx];
                                        KrbEventHandlerFunc handler_func = find_handler(handler_name);
                                        if (handler_func) {
                                            fprintf(debug_file, "INFO: Executing click handler '%s' for element %d\n", handler_name, original_idx);
                                            handler_func();
                                            // Update tab visibility after handler execution
                                            update_tab_visibility(ctx);
                                        }
                                    }
                                    goto end_interaction_check;
                                }
                            }
                         }
                    }
                    break;
                }
            }
        }
        end_interaction_check:;

        // --- Drawing ---
        BeginDrawing();
        Color clear_color = BLACK;
        if (app_element) {
            clear_color = app_element->bg_color;
        } else if (root_count > 0) {
            clear_color = root_elements[0]->bg_color;
        }
        ClearBackground(clear_color);

        // Render roots
        for (int i = 0; i < root_count; ++i) {
            if (root_elements[i]) {
                render_element(root_elements[i], 0, 0, ctx->window_width, ctx->window_height, ctx->scale_factor, debug_file);
            }
        }

        EndDrawing();
    }

    // --- Cleanup ---
    fprintf(debug_file, "INFO: Closing window and cleaning up...\n");
    CloseWindow();

    free_render_context(ctx);
    krb_free_document(&doc);

    if (debug_file != stderr) {
        fclose(debug_file);
    }

    printf("TabBar example finished.\n");
    return 0;
}