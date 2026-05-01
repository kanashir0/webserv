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

test: $(NAME)
	@bash tests/scripts/curl-suite.sh || true

.PHONY: all clean fclean re test

-include $(DEPS)
