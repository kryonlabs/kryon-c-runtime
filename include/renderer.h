#ifndef KRB_RENDERER_H
#define KRB_RENDERER_H

#include <stdio.h>
#include <stdbool.h>
#include "raylib.h"
#include "krb.h"

#ifndef MAX_ELEMENTS
#define MAX_ELEMENTS 256
#endif

#define MAX_LINE_LENGTH 512
#define INVALID_RESOURCE_INDEX 0xFF

// --- Component Instance Tracking ---
typedef struct ComponentInstance {
    uint8_t definition_index;           // Index into KrbDocument's component_defs array
    struct RenderElement* placeholder;  // Original placeholder element
    struct RenderElement* root;         // Root of instantiated component tree
    struct ComponentInstance* next;     // For linked list of instances
} ComponentInstance;

typedef struct RenderElement {
    KrbElementHeader header;
    char* text;
    Color bg_color;
    Color fg_color;
    Color border_color;
    uint8_t border_widths[4];
    uint8_t text_alignment;
    struct RenderElement* parent;
    struct RenderElement* children[MAX_ELEMENTS];
    int child_count;

    // Runtime rendering data
    int render_x;
    int render_y;
    int render_w;
    int render_h;
    bool is_interactive;
    bool is_visible;  // *** ADD THIS LINE ***
    int original_index;

    // Resource handling
    uint8_t resource_index;
    Texture2D texture;
    bool texture_loaded;

    // Component instance tracking
    bool is_component_instance;
    bool is_placeholder;
    ComponentInstance* component_instance;
    
    // Custom properties support
    KrbCustomProperty* custom_properties;
    uint8_t custom_prop_count;

} RenderElement;

// --- Enhanced Document Context ---
typedef struct RenderContext {
    KrbDocument* doc;                   // Original KRB document
    RenderElement* elements;            // Array of all render elements (including instantiated ones)
    int element_count;                  // Total number of elements (original + instantiated)
    int original_element_count;         // Number of original elements from KRB
    ComponentInstance* instances;       // Linked list of component instances
    
    // Rendering state
    Color default_bg;
    Color default_fg;
    Color default_border;
    int window_width;
    int window_height;
    float scale_factor;
    char* window_title;
    bool resizable;
} RenderContext;

// --- Function Declarations ---


// Main rendering function
void render_element(RenderElement* el, int parent_content_x, int parent_content_y, 
    int parent_content_width, int parent_content_height, 
    float scale_factor, FILE* debug_file);

// Component instantiation functions
bool process_component_instances(RenderContext* ctx, FILE* debug_file);
ComponentInstance* instantiate_component(RenderContext* ctx, RenderElement* placeholder, 
                         uint8_t component_def_index, FILE* debug_file);
RenderElement* create_element_from_template(RenderContext* ctx, KrbElementHeader* template_header,
                           KrbProperty* template_properties, int template_prop_count,
                           FILE* debug_file);
void apply_instance_properties(RenderElement* instance_root, RenderElement* placeholder, FILE* debug_file);
bool find_component_name_property(KrbCustomProperty* custom_props, uint8_t custom_prop_count, 
                  char** strings, uint8_t* out_component_index);

// Property and sizing functions
void apply_property_to_element(RenderElement* element, KrbProperty* prop, KrbDocument* doc, FILE* debug_file);
void calculate_element_minimum_size(RenderElement* el, float scale_factor);
void connect_component_children(RenderContext* ctx, FILE* debug_file);

// Setup functions
RenderContext* create_render_context(KrbDocument* doc, FILE* debug_file);
void free_render_context(RenderContext* ctx);

#endif // KRB_RENDERER_H