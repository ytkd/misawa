void *m_alloc(unsigned long int size);
void m_free(void *p);
void mcopy(void *dst, void *src, int size);
void mcopy_2b(void *dst, void *src, int size);
void m_add(void *dst, void *src, int size);
int m_cmp(void *dst, void *src, int size);
void zeromem(void *ptr, int size);
void m_set32(void *ptr, unsigned long int c, int size);
