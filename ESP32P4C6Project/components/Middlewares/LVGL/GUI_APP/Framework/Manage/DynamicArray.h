#ifndef DYNAMIC_ARRAY_H
#define DYNAMIC_ARRAY_H

#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Type aliases for macro concatenation convenience
 * @note These aliases simplify the generic macro template implementation
 */
#define Int     int
#define Float   float
#define Char    char

/**********************************************
 * @def DECLARE_DYNAMIC_ARRAY(Type)
 * @brief Generic macro template to declare dynamic array for a specified data type
 * @param Type Target data type (e.g., int, float, struct Person)
 * @details Generates a dedicated dynamic array structure and all function declarations
 *********************************************/
#define DECLARE_DYNAMIC_ARRAY(Type) \
/**
 * @struct Type##DynamicArray
 * @brief Dynamic array structure for the specified data type
 * @var data: Pointer to the underlying data storage buffer
 * @var size: Current number of valid elements in the array
 * @var capacity: Total allocated memory capacity (number of elements)
 */ \
typedef struct { \
    Type *data;          /* Pointer to the element storage buffer */ \
    size_t size;         /* Current count of valid elements */ \
    size_t capacity;     /* Total allocated memory capacity */ \
} Type##DynamicArray; \
\
/** @brief Initialize the dynamic array */ \
int init##Type##DynamicArray(Type##DynamicArray *array, size_t initialCapacity); \
/** @brief Resize the array's memory capacity */ \
int resize##Type##DynamicArray(Type##DynamicArray *array, size_t newCapacity); \
/** @brief Get the current number of elements */ \
size_t get##Type##Length(Type##DynamicArray *array); \
/** @brief Insert an data at the specified index */ \
void insert##Type##At(Type##DynamicArray *array, size_t index, Type data); \
/** @brief Append an data to the end of the array */ \
void insert##Type##End(Type##DynamicArray *array, Type data); \
/** @brief Delete an data at the specified index */ \
Type delete##Type##At(Type##DynamicArray *array, size_t index); \
/** @brief Delete the last data of the array */ \
Type delete##Type##End(Type##DynamicArray *array); \
/** @brief Destroy the array and free allocated memory */ \
void destroy##Type##DynamicArray(Type##DynamicArray *array); \
/** @brief Print the array with a custom data print function */ \
void print##Type##DynamicArray(Type##DynamicArray *array, void (*printData)(const Type*));

/**********************************************
 * Declare dynamic array implementations for required types
 *********************************************/
DECLARE_DYNAMIC_ARRAY(Int)    // Generate dynamic array for int type
DECLARE_DYNAMIC_ARRAY(Float)  // Generate dynamic array for float type
DECLARE_DYNAMIC_ARRAY(Char)   // Generate dynamic array for char type
// Support for custom structs: DECLARE_DYNAMIC_ARRAY(struct Person)


void dynamicArrayTest();



#endif // DYNAMIC_ARRAY_H