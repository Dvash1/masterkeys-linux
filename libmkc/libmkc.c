/**
 * Author: RedFantom
 * License: GNU GPLv3
 * Copyright (c) 2018 RedFantom
*/
#include "libmkc.h"
#include <pthread.h>
#include <stdlib.h>
#include <time.h>


void sleep(double t) {
    /** Sleep for a specified amount of seconds */
    clock_t start = clock();
    while(((double) (clock() - start)) / CLOCKS_PER_SEC < t);
}


LibMK_Controller* libmk_create_controller(
        LibMK_Device* identifier, LibMK_Model* model) {
    /** Build a new LibMK Controller for a Device */
    LibMK_Handle* device;
    int r = libmk_create_handle(&device, identifier);
    if (r != LIBMK_SUCCESS)
        return NULL;
    LibMK_Controller* controller = (LibMK_Controller*) malloc(
        sizeof(LibMK_Controller));
    controller->device = device;
    pthread_mutex_init(&controller->state_lock, NULL);
    pthread_mutex_init(&controller->exit_flag_lock, NULL);
    pthread_mutex_init(&controller->instr_lock, NULL);
    pthread_mutex_init(&controller->error_lock, NULL);
    return controller;
}


LibMK_Controller_State libmk_get_controller_state(LibMK_Controller* c) {
    /** Return whether the given controller is running (active) */
    pthread_mutex_lock(&c->state_lock);
    LibMK_Controller_State b = c->state;
    pthread_mutex_unlock(&c->state_lock);
    return b;
}


LibMK_Result libmk_get_controller_error(LibMK_Controller* c) {
    /** Return an error if the controller has one set */
    pthread_mutex_lock(&c->error_lock);
    LibMK_Result r = c->error;
    pthread_mutex_unlock(&c->error_lock);
    return r;
}


LibMK_Result libmk_free_controller(LibMK_Controller* c) {
    /** Free the memory allocated for a LibMK_Controller struct */
    if (libmk_get_controller_state(c) == LIBMK_STATE_ACTIVE)
        return LIBMK_ERR_STILL_ACTIVE;
    pthread_mutex_destroy(&c->state_lock);
    pthread_mutex_destroy(&c->exit_flag_lock);
    pthread_mutex_destroy(&c->instr_lock);
    pthread_mutex_destroy(&c->error_lock);
    int r = libmk_free_handle(c->device);
    if (r != LIBMK_SUCCESS)
        return (LibMK_Result) r;
    free(c);
    return LIBMK_SUCCESS;
    
}


LibMK_Result libmk_start_controller(LibMK_Controller* controller) {
    /** Start a LibMK_Controller in a new thread */
    LibMK_Result r = (LibMK_Result) libmk_enable_control(controller->device);
    if (r != LIBMK_SUCCESS)
        return r;
    pthread_create(
        &controller->thread, NULL,
        (void*) libmk_run_controller, (void*) &controller);
    return LIBMK_SUCCESS;
}


void libmk_run_controller(LibMK_Controller* controller) {
    /** Execute instructions on a LibMK_Controller in a separate thread */
    bool exit_flag;
    while (true) {
        pthread_mutex_lock(&controller->exit_flag_lock);
        exit_flag = controller->exit_flag;
        pthread_mutex_unlock(&controller->exit_flag_lock);
        if (exit_flag)
            break;
        
        if (controller->instr == NULL)
            continue;
        LibMK_Result r = (LibMK_Result) libmk_exec_instruction(
            controller->device, controller->instr);
        if (r != LIBMK_SUCCESS) {
            libmk_set_controller_error(controller, r);
            break;
        } else {
            // Move on to next instruction
            LibMK_Instruction* old = controller->instr;
            controller->instr = controller->instr->next;
            libmk_free_instruction(old);
        }
    }
    int r = libmk_disable_control(controller->device);
    if (r != LIBMK_SUCCESS) {
        libmk_set_controller_error(controller, (LibMK_Result) r);
    }
}


void libmk_set_controller_error(LibMK_Controller* c, LibMK_Result e) {
    /** Set an error in the controller struct if one has not been set */
    pthread_mutex_lock(&c->error_lock);
    if (c->error == LIBMK_SUCCESS)
        c->error = e;
    pthread_mutex_unlock(&c->error_lock);
}


int libmk_exec_instruction(LibMK_Handle* h, LibMK_Instruction* i) {
    /** Execute a single instruction on the keyboard controlled */
    if (i == NULL)
        return libmk_send_control_packet(h);
    else if (i->colors != NULL) {
        return libmk_set_all_led_color(h, i->colors);
    } else {
        unsigned char colors[LIBMK_MAX_ROWS][LIBMK_MAX_COLS][3];
        for (unsigned char r = 0; r < LIBMK_MAX_ROWS; r++)
            for (unsigned char c = 0; c < LIBMK_MAX_COLS; c++)
                for (unsigned char k = 0; k < 3; k++)
                    colors[r][c][k] = i->color[k];
        return libmk_set_all_led_color(h, (unsigned char*) colors);
    }
}


void libmk_stop_controller(LibMK_Controller* controller) {
    /** Stop a LibMK_Controller running in a separate thread */
    pthread_mutex_lock(&controller->exit_flag_lock);
    controller->exit_flag = true;
    pthread_mutex_unlock(&controller->exit_flag_lock);
}


LibMK_Controller_State libmk_join_controller(
        LibMK_Controller* controller, double timeout) {
    /** Join a LibMK_Controller into the current thread (return state) */
    LibMK_Controller_State s;
    clock_t t = clock();
    double elapsed;
    while (true) {
        pthread_mutex_lock(&controller->state_lock);
        s = controller->state;
        pthread_mutex_unlock(&controller->state_lock);
        if (s != LIBMK_STATE_ACTIVE)
            break;
        elapsed = ((double) (clock() - t)) / CLOCKS_PER_SEC;
        if (elapsed > timeout)
            return LIBMK_STATE_JOIN_ERR;
    }
    return s;
}


void libmk_free_instruction(LibMK_Instruction* i) {
    /** Free the memory allocated for an executed instruction */
    if (i->colors != NULL)
        free(i->colors);
    free(i);
}


LibMK_Instruction* libmk_create_instruction() {
    /** Allocate a new LibMK_Instruction struct */
    LibMK_Instruction* i =
        (LibMK_Instruction*) malloc(sizeof(LibMK_Instruction));
    i->duration = 0;
    i->id = 0;
    return i;
}


LibMK_Instruction* libmk_create_instruction_full(unsigned char c[3]) {
    /** Create a new instruction that sets a full LED color */
    LibMK_Instruction* i = libmk_create_instruction();
    for (unsigned char j=0; j<3; j++)
        i->color[j] = c[j];
    return i;
}


LibMK_Instruction* libmk_create_instruction_all(
        unsigned char c[LIBMK_MAX_ROWS][LIBMK_MAX_COLS][3]) {
    /** Create a new instruction that sets the color of all individual LEDs */
    LibMK_Instruction* i = libmk_create_instruction();
    i->colors = (unsigned char*) malloc(
        sizeof(unsigned char) * LIBMK_MAX_ROWS * LIBMK_MAX_COLS * 3);
    for (unsigned char row=0; row<LIBMK_MAX_ROWS; row++)
        for (unsigned char col=0; col<LIBMK_MAX_COLS; col++)
            for (unsigned char j=0; j<3; j++)
                i->colors[row * LIBMK_MAX_COLS + j] = c[row][col][j];
    return i;
}


LibMK_Result libmk_sched_instruction(
    LibMK_Controller* c, LibMK_Instruction* i) {
    /** Schedule a single instruction for execution on the controller */
    pthread_mutex_lock(&c->instr_lock);
    if (c->instr == NULL) {
        c->instr = i;
        c->instr->id = 1;
    } else {
        LibMK_Instruction* t = c->instr;
        while (true) {
            if (t->next == NULL) {
                t->next = i;
                t->next->id = t->id + 1;
                break;
            }
            t = t->next;
        }
    }
    pthread_mutex_unlock(&c->instr_lock);
    return LIBMK_SUCCESS;
}


LibMK_Result libmk_cancel_instruction(LibMK_Controller* c, unsigned int id) {
    /** Cancel a scheduled instruction from executing */
    pthread_mutex_lock(&c->instr_lock);
    LibMK_Instruction* prev = NULL;
    LibMK_Instruction* curr = c->instr;
    while (curr != NULL) {
        if (curr->id == id) {
            // Cancel this instruction
            if (prev != NULL)
                prev->next = curr->next;
            else  // First instruction in Linked List
                c->instr = curr->next;
            libmk_free_instruction(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    pthread_mutex_unlock(&c->instr_lock);
    return LIBMK_SUCCESS;
}