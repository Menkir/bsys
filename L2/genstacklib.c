#include "genstacklib.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

int stack_created = 0;

void GenStackNew(genStack *s, int elemSize, void(*freefn)(void*))
{
    s->elems = malloc(GenStackInitialAllocationSize * elemSize);
    assert(s->elems != NULL);
    
    s->elemSize = elemSize;
    s->logLength = 0; 
    s->allocLength = GenStackInitialAllocationSize;
    s->freefn = freefn;
    
}
void GenStackDispose(genStack *s)
{
	char *ptr1 = (char*) s->elems;
	int i;
	for(i = 0; i < s->logLength; ++i)
	{
			free(ptr1+i);
	}
	
	free(ptr1);
    
}
void GenStackPush(genStack *s, const void *elemAddr)
{
    if(s->logLength == s->allocLength)
    {
        s->elems = realloc(s->elems, s->allocLength * 2 *  s->elemSize);
		s->allocLength *= 2;
		assert(s->elems != NULL);
    }
   
    char *ptr1 = (char*) s->elems;
    ptr1 += s->logLength * s->elemSize; 
    
    char *ptr2 = (char*) elemAddr;
    
    strncpy(ptr1, ptr2, s->elemSize);	

    s->logLength++;
}
void GenStackPop(genStack *s, void *elemAddr)
{
	char *ptr1 = (char*) s->elems;
    ptr1 += (s->logLength-1) * s->elemSize; 
    
    char *ptr2 = (char*) elemAddr;
    
    strncpy(ptr2, ptr1, s->elemSize);	
	
    s->logLength--;
}
bool GenStackEmpty(const genStack *s)
{
	if(s->logLength == 0)
	{
		return true;
	}
	else
	{
		return false;
	}
}

