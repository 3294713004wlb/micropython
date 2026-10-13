# Preserve the original user's v1.23 Asyncify profile.
# Asyncify's async stack is separate from the Emscripten C stack.
JSFLAGS += -s ASYNCIFY
JSFLAGS += -s ASYNCIFY_STACK_SIZE=262144
# Long-lived browser Workers are terminated by their owner. Avoid Node-style runtime exit.
JSFLAGS += -s EXIT_RUNTIME=0
