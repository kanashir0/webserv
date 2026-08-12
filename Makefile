NAME      := webserv

CXX       := c++
CXXFLAGS  := -Wall -Wextra -Werror -std=c++98 -pedantic
INCLUDES  := -Iinclude

SRC_DIR   := src
OBJ_DIR   := build

SRCS      := $(shell find $(SRC_DIR) -name '*.cpp')
OBJS      := $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)
DEPS      := $(OBJS:.o=.d)

RM        := rm -rf

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

clean:
	$(RM) $(OBJ_DIR)

fclean: clean
	$(RM) $(NAME)

re: fclean all

# Sem `|| true`: um teste que falha precisa falhar o alvo, senão a suíte é
# decorativa. As três suítes rodam sempre; o exit code é o da última que falhou.
test: $(NAME)
	@rc=0; \
	for s in curl-suite test-edge-cases test-multi-server; do \
		echo "=== $$s ==="; \
		bash tests/scripts/$$s.sh || rc=1; \
	done; \
	exit $$rc

.PHONY: all clean fclean re test

-include $(DEPS)
