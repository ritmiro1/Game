// Заглушка единственной функции OpenGL, которую игра запрашивает напрямую.
const unsigned char *glGetString(unsigned int name) {
    (void)name;
    return (const unsigned char *)"stub-renderer";
}
