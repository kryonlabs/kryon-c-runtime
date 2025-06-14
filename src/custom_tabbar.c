#include "custom_tabbar.h"
#include <string.h>
#include <stdio.h>

void register_tabbar_component(void) {
    register_custom_component("TabBar", handle_tabbar_component);
}
bool handle_tabbar_component(RenderContext* ctx, RenderElement* element, FILE* debug_file) {
    if (!ctx || !element) return false;
    
    if (debug_file) {
        fprintf(debug_file, "INFO: Processing TabBar component (Element %d)\n", element->original_index);
    }
    
    // Get custom properties from the original placeholder
    ComponentInstance* instance = element->component_instance;
    if (!instance || !instance->placeholder) {
        if (debug_file) fprintf(debug_file, "  ERROR: No component instance or placeholder found\n");
        return false;
    }
    
    const char* position = get_custom_property_value(instance->placeholder, "position", ctx->doc);
    if (!position) position = "bottom";
    
    const char* orientation = get_custom_property_value(instance->placeholder, "orientation", ctx->doc);
    if (!orientation) orientation = "row";
    
    if (debug_file) {
        fprintf(debug_file, "  TabBar position:'%s' orientation:'%s' children:%d parent:%p\n", 
                position, orientation, element->child_count, (void*)element->parent);
    }
    
    // Calculate TabBar size
    float tabbar_size = 50.0f * ctx->scale_factor;
    
    // Set TabBar size
    if (strcmp(orientation, "row") == 0) {
        element->render_w = element->parent ? element->parent->render_w : ctx->window_width;
        element->render_h = (int)tabbar_size;
    } else {
        element->render_w = (int)tabbar_size;
        element->render_h = element->parent ? element->parent->render_h : ctx->window_height;
    }
    
    // Position TabBar and adjust siblings
    if (element->parent) {
        if (strcmp(position, "bottom") == 0) {
            // Position TabBar at bottom
            element->render_x = element->parent->render_x;
            element->render_y = element->parent->render_y + element->parent->render_h - element->render_h;
            
            if (debug_file) {
                fprintf(debug_file, "  TabBar positioned at bottom: (%d,%d) %dx%d\n", 
                        element->render_x, element->render_y, element->render_w, element->render_h);
                fprintf(debug_file, "  Parent has %d children\n", element->parent->child_count);
            }
            
            // Find and adjust the main content sibling
            for (int i = 0; i < element->parent->child_count; i++) {
                RenderElement* sibling = element->parent->children[i];
                if (sibling && sibling != element) {
                    if (debug_file) {
                        fprintf(debug_file, "  Found sibling %d: (%d,%d) %dx%d\n", 
                                sibling->original_index, sibling->render_x, sibling->render_y, 
                                sibling->render_w, sibling->render_h);
                    }
                    
                    // Adjust sibling to make room for TabBar
                    sibling->render_x = element->parent->render_x;
                    sibling->render_y = element->parent->render_y;
                    sibling->render_w = element->parent->render_w;
                    sibling->render_h = element->render_y - element->parent->render_y;
                    
                    // Ensure minimum size
                    if (sibling->render_w < 1) sibling->render_w = 1;
                    if (sibling->render_h < 1) sibling->render_h = 1;
                    
                    if (debug_file) {
                        fprintf(debug_file, "  Adjusted sibling %d to: (%d,%d) %dx%d\n", 
                                sibling->original_index, sibling->render_x, sibling->render_y, 
                                sibling->render_w, sibling->render_h);
                    }
                    
                    break; // Only adjust the first sibling (main content)
                }
            }
        }
        // Add other position handling (top, left, right) as needed
    }
    
    // Layout TabBar children (buttons)
    layout_tabbar_children(element, orientation, debug_file);
    
    if (debug_file) {
        fprintf(debug_file, "  TabBar final frame: (%d,%d) %dx%d\n", 
                element->render_x, element->render_y, element->render_w, element->render_h);
    }
    
    return true;
}

void adjust_sibling_for_tabbar(RenderElement* tabbar, const char* position, FILE* debug_file) {
    if (!tabbar->parent || tabbar->parent->child_count <= 1) return;
    
    // Find the main content sibling (usually the first non-TabBar child)
    RenderElement* main_content = NULL;
    for (int i = 0; i < tabbar->parent->child_count; i++) {
        if (tabbar->parent->children[i] && tabbar->parent->children[i] != tabbar) {
            main_content = tabbar->parent->children[i];
            break;
        }
    }
    
    if (!main_content) return;
    
    // Adjust main content to make room for TabBar
    if (strcmp(position, "bottom") == 0) {
        // TabBar at bottom: reduce main content height
        main_content->render_x = tabbar->parent->render_x;
        main_content->render_y = tabbar->parent->render_y;
        main_content->render_w = tabbar->parent->render_w;
        main_content->render_h = tabbar->render_y - tabbar->parent->render_y;
    } else if (strcmp(position, "top") == 0) {
        // TabBar at top: move main content down and reduce height
        main_content->render_x = tabbar->parent->render_x;
        main_content->render_y = tabbar->render_y + tabbar->render_h;
        main_content->render_w = tabbar->parent->render_w;
        main_content->render_h = (tabbar->parent->render_y + tabbar->parent->render_h) - main_content->render_y;
    }
    
    // Ensure minimum size
    if (main_content->render_w < 1) main_content->render_w = 1;
    if (main_content->render_h < 1) main_content->render_h = 1;
    
    if (debug_file) {
        fprintf(debug_file, "  Adjusted main content: (%d,%d) %dx%d\n",
                main_content->render_x, main_content->render_y,
                main_content->render_w, main_content->render_h);
    }
}

void layout_tabbar_children(RenderElement* tabbar, const char* orientation, FILE* debug_file) {
    if (!tabbar || tabbar->child_count == 0) return;
    
    int content_x = tabbar->render_x;
    int content_y = tabbar->render_y;
    int content_w = tabbar->render_w;
    int content_h = tabbar->render_h;
    
    if (strcmp(orientation, "row") == 0) {
        // Distribute children horizontally
        int button_width = content_w / tabbar->child_count;
        for (int i = 0; i < tabbar->child_count; i++) {
            if (tabbar->children[i]) {
                tabbar->children[i]->render_x = content_x + i * button_width;
                tabbar->children[i]->render_y = content_y;
                tabbar->children[i]->render_w = button_width;
                tabbar->children[i]->render_h = content_h;
                
                if (debug_file) {
                    fprintf(debug_file, "    TabBar button %d: (%d,%d) %dx%d\n", i,
                            tabbar->children[i]->render_x, tabbar->children[i]->render_y,
                            tabbar->children[i]->render_w, tabbar->children[i]->render_h);
                }
            }
        }
    } else {
        // Distribute children vertically
        int button_height = content_h / tabbar->child_count;
        for (int i = 0; i < tabbar->child_count; i++) {
            if (tabbar->children[i]) {
                tabbar->children[i]->render_x = content_x;
                tabbar->children[i]->render_y = content_y + i * button_height;
                tabbar->children[i]->render_w = content_w;
                tabbar->children[i]->render_h = button_height;
                
                if (debug_file) {
                    fprintf(debug_file, "    TabBar button %d: (%d,%d) %dx%d\n", i,
                            tabbar->children[i]->render_x, tabbar->children[i]->render_y,
                            tabbar->children[i]->render_w, tabbar->children[i]->render_h);
                }
            }
        }
    }
}