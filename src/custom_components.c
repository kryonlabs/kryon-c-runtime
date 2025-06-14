#include "custom_components.h"
#include <string.h>
#include <stdio.h>

// Registry of custom component handlers
static CustomComponentRegistration custom_handlers[MAX_CUSTOM_COMPONENTS];
static int handler_count = 0;

const char* get_custom_property_value(RenderElement* element, const char* prop_name, KrbDocument* doc) {
    if (!element || !prop_name || !doc || !element->custom_properties) return NULL;
    
    for (uint8_t i = 0; i < element->custom_prop_count; i++) {
        KrbCustomProperty* prop = &element->custom_properties[i];
        
        if (prop->key_index < doc->header.string_count && doc->strings[prop->key_index] &&
            strcmp(doc->strings[prop->key_index], prop_name) == 0) {
            
            if (prop->value_type == VAL_TYPE_STRING && prop->value_size == 1 && prop->value) {
                uint8_t value_idx = *(uint8_t*)prop->value;
                if (value_idx < doc->header.string_count && doc->strings[value_idx]) {
                    return doc->strings[value_idx];
                }
            }
        }
    }
    return NULL;
}

float max_f(float a, float b) {
    return (a > b) ? a : b;
}

bool register_custom_component(const char* name, CustomComponentHandler handler) {
    if (handler_count >= MAX_CUSTOM_COMPONENTS) {
        fprintf(stderr, "ERROR: Too many custom components registered\n");
        return false;
    }
    
    custom_handlers[handler_count].component_name = name;
    custom_handlers[handler_count].handler = handler;
    handler_count++;
    return true;
}

bool process_custom_components(RenderContext* ctx, FILE* debug_file) {
    if (!ctx) return false;
    
    if (debug_file) {
        fprintf(debug_file, "INFO: Processing custom components...\n");
    }
    
    // Process component instances
    ComponentInstance* instance = ctx->instances;
    while (instance) {
        if (instance->root && instance->definition_index < ctx->doc->header.component_def_count) {
            KrbComponentDefinition* comp_def = &ctx->doc->component_defs[instance->definition_index];
            
            if (comp_def->name_index < ctx->doc->header.string_count && ctx->doc->strings[comp_def->name_index]) {
                const char* comp_name = ctx->doc->strings[comp_def->name_index];
                
                // Find matching handler
                for (int i = 0; i < handler_count; i++) {
                    if (strcmp(comp_name, custom_handlers[i].component_name) == 0) {
                        if (debug_file) {
                            fprintf(debug_file, "  Found handler for component '%s' (Element %d)\n", 
                                    comp_name, instance->root->original_index);
                        }
                        custom_handlers[i].handler(ctx, instance->root, debug_file);
                        break;
                    }
                }
            }
        }
        instance = instance->next;
    }
    
    if (debug_file) {
        fprintf(debug_file, "INFO: Finished processing custom components\n");
    }
    
    return true;
}

void init_custom_components(void) {
    handler_count = 0;
    
    // Register all custom components
    register_tabbar_component();
    // Add more component registrations here:
    // register_modal_component();
    // register_dropdown_component();
    // etc.
}