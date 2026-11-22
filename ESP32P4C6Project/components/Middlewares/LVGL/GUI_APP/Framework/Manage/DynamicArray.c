#include "DynamicArray.h"
#include <stdio.h>
#include <stdlib.h>

/**********************************************
 * @file DynamicArray.c
 * @brief Generic dynamic array implementation using macro templates
 * @details Supports automatic capacity expansion, basic CRUD operations for any data type
 * @note This file implements the dynamic array functions for specified types via macros
 *********************************************/

/**********************************************
 * @def IMPLEMENT_DYNAMIC_ARRAY(Type)
 * @brief Macro template to generate dynamic array functions for a specific type
 * @param Type Prefix name of the target data type (e.g., Int for int type)
 * @usage Call this macro to instantiate a dynamic array for the target type
 *********************************************/
#define IMPLEMENT_DYNAMIC_ARRAY(Type) \
/**
 * @brief Initialize a dynamic array
 * @param array Pointer to the dynamic array structure
 * @param initialCapacity Initial reserved capacity of the array
 * @return int 0 on success, -1 if memory allocation fails
 */ \
int init##Type##DynamicArray(Type##DynamicArray *array, size_t initialCapacity) { \
    /* Set default capacity to 2 to avoid zero capacity */ \
    if (initialCapacity == 0) initialCapacity = 2; \
    \
    array->data = (Type*)malloc(initialCapacity * sizeof(Type)); \
    if (array->data == NULL) { \
        return -1; \
    } \
    \
    array->size = 0; \
    array->capacity = initialCapacity; \
    return 0; \
} \
\
/**
 * @brief Resize the dynamic array's capacity
 * @param array Pointer to the dynamic array structure
 * @param newCapacity New capacity to set
 * @return int 0 on success, -1 if memory reallocation fails
 */ \
int resize##Type##DynamicArray(Type##DynamicArray *array, size_t newCapacity) { \
    Type *newData = (Type*)realloc(array->data, newCapacity * sizeof(Type)); \
    if (newData == NULL) { \
        return -1; \
    } \
    \
    array->data = newData; \
    array->capacity = newCapacity; \
    return 0; \
} \
\
/**
 * @brief Get the number of elements in the dynamic array
 * @param array Pointer to the dynamic array structure
 * @return size_t Current element count (size) of the array
 */ \
size_t get##Type##Length(Type##DynamicArray *array) { \
    return array->size; \
} \
\
/**
 * @brief Insert an data at the specified index
 * @param array Pointer to the dynamic array structure
 * @param index Target insertion position (0 <= index <= size)
 * @param data The data to insert
 * @note Automatically expands capacity (doubles) when full
 */ \
void insert##Type##At(Type##DynamicArray *array, size_t index, Type data) { \
    /* size_t is unsigned, index < 0 is an invalid check */ \
    if (index > array->size) return; \
    \
    /* Expand capacity when array is full */ \
    if (array->size == array->capacity) { \
        resize##Type##DynamicArray(array, array->capacity * 2); \
    } \
    \
    /* Shift elements backward to make space */ \
    for (size_t i = array->size; i > index; i--) { \
        array->data[i] = array->data[i-1]; \
    } \
    \
    array->data[index] = data; \
    array->size++; \
} \
\
/**
 * @brief Append an data to the end of the dynamic array
 * @param array Pointer to the dynamic array structure
 * @param data The data to insert
 */ \
void insert##Type##End(Type##DynamicArray *array, Type data) { \
    insert##Type##At(array, array->size, data); \
} \
\
/**
 * @brief Delete an data at the specified index
 * @param array Pointer to the dynamic array structure
 * @param index Target deletion position (0 <= index < size)
 * @return Type The deleted data; returns a default value if index is invalid
 * @warning Return value -1 is only valid for numeric types; adjust for non-numeric types
 */ \
Type delete##Type##At(Type##DynamicArray *array, size_t index){ \
    if (index >= array->size) { \
        return -1; \
    } \
    \
    Type deletedData = array->data[index]; \
    /* Shift elements forward to fill the gap */ \
    for (size_t i = index; i < array->size - 1; i++) { \
        array->data[i] = array->data[i+1]; \
    } \
    \
    array->size--; \
    return deletedData; \
} \
\
/**
 * @brief Delete the last data of the dynamic array
 * @param array Pointer to the dynamic array structure
 * @return Type The deleted last data
 */ \
Type delete##Type##End(Type##DynamicArray *array){ \
    return delete##Type##At(array, array->size - 1); \
} \
\
/**
 * @brief Destroy the dynamic array and free allocated memory
 * @param array Pointer to the dynamic array structure
 * @note Resets size/capacity to 0 and data pointer to NULL to avoid dangling pointers
 */ \
void destroy##Type##DynamicArray(Type##DynamicArray *array) { \
    free(array->data); \
    array->data = NULL; \
    array->size = 0; \
    array->capacity = 0; \
} \
\
/**
 * @brief Print the dynamic array with a custom data print function
 * @param array Pointer to the dynamic array structure
 * @param printData Function pointer to print a single data
 */ \
void print##Type##DynamicArray(Type##DynamicArray *array, void (*printData)(const Type*)) { \
    printf("[%s Array] ", #Type); \
    for (size_t i = 0; i < array->size; i++) { \
        printData(&array->data[i]); \
        printf(" "); \
    } \
    printf("size: %zu, capacity: %zu Bytes\n", array->size, array->capacity * sizeof(Type)); \
}

/**********************************************
 * Instantiate dynamic array implementations for common types
 *********************************************/
// Implement integer type dynamic array
IMPLEMENT_DYNAMIC_ARRAY(Int)
// Implement float type dynamic array
IMPLEMENT_DYNAMIC_ARRAY(Float)
// Implement char type dynamic array
IMPLEMENT_DYNAMIC_ARRAY(Char)





// 自定义int类型的打印函数（适配printintDynamicArray的函数指针参数）
void printIntElem(const int* elem) {
    printf("%d", *elem);
}

// 自定义int类型的打印函数（适配printintDynamicArray的函数指针参数）
void printFloatElem(const float* elem) {
    printf("%.2f", *elem);
}

// 自定义int类型的打印函数（适配printintDynamicArray的函数指针参数）
void printCharElem(const char* elem) {
    printf("%c", *elem);
}



void dynamicArrayTest(){
	IntDynamicArray intArr;
	initIntDynamicArray(&intArr, 4);

	for(int i = 0; i < 8; i++){
		insertIntEnd(&intArr,i);
	}
	printIntDynamicArray(&intArr,printIntElem);

	insertIntAt(&intArr,2,0);
	printIntDynamicArray(&intArr,printIntElem);



	deleteIntAt(&intArr,6);
	printIntDynamicArray(&intArr,printIntElem);

	deleteIntEnd(&intArr);
	printIntDynamicArray(&intArr,printIntElem);





	FloatDynamicArray floatArr;
	initFloatDynamicArray(&floatArr, 4);

	for(int i = 0; i < 8; i++){
		insertFloatEnd(&floatArr,i);
	}
	printFloatDynamicArray(&floatArr, printFloatElem);

	insertFloatAt(&floatArr,2,0);
	printFloatDynamicArray(&floatArr, printFloatElem);



	deleteFloatAt(&floatArr,6);
	printFloatDynamicArray(&floatArr, printFloatElem);

	deleteFloatEnd(&floatArr);
	printFloatDynamicArray(&floatArr, printFloatElem);


	CharDynamicArray charArr;
    initCharDynamicArray(&charArr, 4);

    // 插入字母a~h（共8个，触发一次扩容）
    for(int i = 0; i < 8; i++){
        insertCharEnd(&charArr, 'a' + i);
    }
    printCharDynamicArray(&charArr, printCharElem);

    // 在索引2位置插入字符'0'
    insertCharAt(&charArr, 2, '0');
    printCharDynamicArray(&charArr, printCharElem);

    // 删除索引6位置的元素
    deleteCharAt(&charArr, 6);
    printCharDynamicArray(&charArr, printCharElem);

    // 删除末尾元素
    deleteCharEnd(&charArr);
    printCharDynamicArray(&charArr, printCharElem);

    // 销毁所有数组（避免内存泄漏，建议补充）
    destroyIntDynamicArray(&intArr);
    destroyFloatDynamicArray(&floatArr);
    destroyCharDynamicArray(&charArr);



}


