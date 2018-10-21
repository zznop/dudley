/**
 * build.c
 *
 * Copyright (C) 2018 zznop, zznop0x90@gmail.com
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "build.h"
#include "utils.h"
#include "conversion.h"
#include "parson/parson.h"

// TODO: a lot of arguments are being passed to this function - need to clean this up
static int push_block(const char *name, dud_t *ctx, uint8_t *start,
    size_t size, struct json_array_t *length_blocks, endianess_t endian)
{
    struct block_metadata *curr;
    struct block_metadata *tail;
    curr = (struct block_metadata *)malloc(sizeof(*curr));
    if (!curr) {
        duderr("Out of memory");
        return FAILURE;
    }

    memset(curr, '\0', sizeof(*curr));
    curr->name = name;
    curr->start = start;
    curr->size = size;
    curr->length_blocks = length_blocks;
    curr->next = NULL;
    curr->endian = endian;

    if (!ctx->blocks->list) {
        // first element, set head
        ctx->blocks->list = curr;
    } else {
        // iterate until we get to the end and set tail->next
        tail = ctx->blocks->list;
        while (1) {
            if (!tail->next) {
                tail->next = curr;
                break;
            }

            tail = tail->next;
        }
    }

    return SUCCESS;
}

static int realloc_data_buffer(dud_t *ctx, size_t size)
{
    uint8_t *tmp = NULL;

    if (!ctx->buffer.data) {
        ctx->buffer.data = (uint8_t *)malloc(size);
        if (!ctx->buffer.data) {
            duderr("Out of memory");
            return FAILURE;
        }

        ctx->buffer.ptr = ctx->buffer.data;
    } else {
        tmp = realloc(ctx->buffer.data, ctx->buffer.size + size);
        if (!ctx->buffer.data) {
            duderr("Out of memory");
            return FAILURE;
        }

        ctx->buffer.data = tmp;
    }

    ctx->buffer.size += size;
    return SUCCESS;
}

static int consume_hexstr(const char *name, struct json_value_t *block_json_value, dud_t *ctx)
{
    size_t data_size = 0;
    const char *pos = NULL;
    size_t i = 0;
    const char *value;
    uint8_t *start = ctx->buffer.ptr;

    value = json_object_get_string(json_object(block_json_value), "value");
    if (!value) {
        duderr("Failed to read data from block");
        return FAILURE;
    }

    if (value[strspn(value, "0123456789abcdefABCDEF")]) {
        duderr("Input data is not a valid hex string");
        return FAILURE;
    }

    data_size = strlen(value) / 2;
    if (realloc_data_buffer(ctx, data_size))
        return FAILURE;

    pos = value;
    for (i = 0; i < data_size; i++, ctx->buffer.ptr++) {
        sscanf(pos, "%2hhx", ctx->buffer.ptr);
        pos += 2;
    }

    push_block(name, ctx, start, data_size,
        json_object_get_array(json_object(block_json_value), "length-blocks"), IRREND);
    
    return SUCCESS;
}

static endianess_t get_endianess(struct json_value_t *block_json_value)
{
    const char *value = json_object_get_string(json_object(block_json_value), "endianess");
    if (!value)
        return BIGEND;

    if (!strcmp(value, "little"))
        return LITEND;
    else if (!strcmp(value, "big"))
        return BIGEND;

    duderr("Erroneous endian specification: %s", value);
    return ERREND;
}

static int consume_qword(const char *name, struct json_value_t *block_json_value, dud_t *ctx)
{
    uint64_t qword;
    const char *value;
    endianess_t endian = get_endianess(block_json_value);
    if (endian == ERREND) {
        return FAILURE;
    }

    value = json_object_get_string(json_object(block_json_value), "value");
    if (!value) {
        duderr("Failed to retrieve dword value from block");
        return FAILURE;
    }

    qword = hexstr_to_qword(value, endian);
    if (qword == 0 && errno != 1) {
        duderr("JSON value cannot be represented as a qword: %s", value);
        return FAILURE;
    }

    if (realloc_data_buffer(ctx, sizeof(qword)))
        return FAILURE;

    memcpy(ctx->buffer.ptr, &qword, sizeof(qword));
    push_block(name, ctx, ctx->buffer.ptr, sizeof(qword),
        json_object_get_array(json_object(block_json_value), "length-blocks"), endian);
    ctx->buffer.ptr += sizeof(qword);

    return SUCCESS;
}

static int consume_dword(const char *name, struct json_value_t *block_json_value, dud_t *ctx)
{
    uint32_t dword;
    const char *value;
    endianess_t endian = get_endianess(block_json_value);
    if (endian == ERREND) {
        return FAILURE;
    }

    value = json_object_get_string(json_object(block_json_value), "value");
    if (!value) {
        duderr("Failed to retrieve dword value from block");
        return FAILURE;
    }

    dword = hexstr_to_dword(value, endian);
    if (dword == 0 && errno != 1) {
        duderr("JSON value cannot be represented as a dword: %s", value);
        return FAILURE;
    }

    if (realloc_data_buffer(ctx, sizeof(dword)))
        return FAILURE;

    memcpy(ctx->buffer.ptr, &dword, sizeof(dword));
    push_block(name, ctx, ctx->buffer.ptr, sizeof(dword),
        json_object_get_array(json_object(block_json_value), "length-blocks"), endian);
    ctx->buffer.ptr += sizeof(dword);

    return SUCCESS;
}

static int consume_word(const char *name, struct json_value_t *block_json_value, dud_t *ctx)
{
    uint16_t word;
    const char *value;
    endianess_t endian = get_endianess(block_json_value);
    if (endian == ERREND) {
        return FAILURE;
    }

    value = json_object_get_string(json_object(block_json_value), "value");
    if (!value) {
        duderr("Failed to retrieve dword value from block");
        return FAILURE;
    }

    word = hexstr_to_word(value, endian);
    if (word == 0 && errno != 0) {
        duderr("JSON value cannot be represented as a word: %s", value);
        return FAILURE;
    }

    if (realloc_data_buffer(ctx, sizeof(word)))
        return FAILURE;

    memcpy(ctx->buffer.ptr, &word, sizeof(word));
    push_block(name, ctx, ctx->buffer.ptr, sizeof(word),
        json_object_get_array(json_object(block_json_value), "length-blocks"), endian);
    ctx->buffer.ptr += sizeof(word);

    return SUCCESS;
}

static int consume_byte(const char *name, struct json_value_t *block_json_value, dud_t *ctx)
{
    uint8_t byte;
    const char *value;

    value = json_object_get_string(json_object(block_json_value), "value");
    if (!value) {
        duderr("Failed to retrieve byte value from block");
        return FAILURE;
    }

    byte = hexstr_to_byte(value);
    if (byte == 0 && errno != 0) {
        duderr("JSON value cannot be converted to a byte: %s", value);
        return FAILURE;
    }

    if (realloc_data_buffer(ctx, sizeof(byte)))
        return FAILURE;

    memcpy(ctx->buffer.ptr, &byte, sizeof(byte));
    push_block(name, ctx, ctx->buffer.ptr, sizeof(byte),
        json_object_get_array(json_object(block_json_value), "length-blocks"), IRREND);
    ctx->buffer.ptr += sizeof(byte);

    return SUCCESS;
}

static int consume_number(const char *name, struct json_value_t *block_json_value, dud_t *ctx)
{
    const char *type = json_object_get_string(json_object(block_json_value), "type");
    if (!type) {
        duderr("Failed to retrieve number type");
        return FAILURE;
    }

    // TODO make this a hashmap
    if (!strcmp(type, "qword"))
        return consume_qword(name, block_json_value, ctx);
    else if (!strcmp(type, "dword"))
        return consume_dword(name, block_json_value, ctx);
    else if (!strcmp(type, "word"))
        return consume_word(name, block_json_value, ctx);
    else if (!strcmp(type, "byte"))
        return consume_byte(name, block_json_value, ctx);

    duderr("Unsupported number type: %s\n", type);
    return FAILURE;
}

static int reserve_length(const char *name, struct json_value_t *block_json_value, dud_t *ctx)
{
    const char *type;
    endianess_t endian;
    size_t size;

    type = json_object_get_string(json_object(block_json_value), "type");
    if (!type) {
        duderr("Failed to parse type for length field");
        return FAILURE;
    }

    endian = get_endianess(block_json_value);

    // TODO make this a map
    if (!strcmp(type, "qword")) {
        size = sizeof(uint64_t);
    } else if (!strcmp(type, "dword")) {
        size = sizeof(uint32_t);
    } else if (!strcmp(type, "word")) {
        size = sizeof(uint16_t);
    } else if (!strcmp(type, "byte")) {
        size = sizeof(uint8_t);
    } else {
        duderr("Unsupported length type: %s", type);
        return FAILURE;
    }

    if (realloc_data_buffer(ctx, size))
        return FAILURE;

    memset(ctx->buffer.ptr, 0, size);
    push_block(name, ctx, ctx->buffer.ptr, size,
        json_object_get_array(json_object(block_json_value), "length-blocks"), endian);
    ctx->buffer.ptr += size;
    return SUCCESS;    
}

static int handle_block(const char * name, struct json_value_t *block_json_value, dud_t *ctx)
{
    const char *class = json_object_get_string(json_object(block_json_value), "class");
    if (!class) {
        duderr("Failed to retrieve block class");
        return FAILURE;
    }

    if (!strcmp(class, "hex"))
        return consume_hexstr(name, block_json_value, ctx);
    else if (!strcmp(class, "number"))
        return consume_number(name, block_json_value, ctx);
    else if (!strcmp(class, "length"))
        return reserve_length(name, block_json_value, ctx);

    duderr("Unsupported block class: %s\n", class);
    return FAILURE;
}

int iterate_blocks(dud_t *ctx)
{
    struct json_value_t *block_json_value = NULL;
    const char *name;
    for (ctx->blocks->idx = 0; ctx->blocks->idx < ctx->blocks->count; ctx->blocks->idx++) {
        name = json_object_get_name(json_object(ctx->blocks->json_value), ctx->blocks->idx);
        dudinfo("  -- %s", name);
        block_json_value = json_object_get_value_at(
                json_object(ctx->blocks->json_value), ctx->blocks->idx);
        if (!block_json_value) {
            duderr("Failed to retrieve next JSON block (idx: %lu", ctx->blocks->idx);
            return FAILURE;
        }

        if (handle_block(name, block_json_value, ctx) == FAILURE)
            return FAILURE;
    }

    return SUCCESS;
}