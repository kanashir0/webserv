NAME      := webserv

GREEN     := \033[0;32m
YELLOW    := \033[0;33m
BLUE      := \033[0;34m
RED       := \033[0;31m
BOLD      := \033[1m
DIM       := \033[2m
NC        := \033[0m

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
	@$(CXX) $(CXXFLAGS) $(OBJS) -o $@
	@echo "$(GREEN)$(BOLD)[OK]$(NC)         $(BOLD)$(NAME)$(NC) $(DIM)($(words $(OBJS)) objetos, $(CXXFLAGS))$(NC)"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "$(BLUE)[Compilando]$(NC) $<"
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

clean:
	@$(RM) $(OBJ_DIR)
	@echo "$(YELLOW)[Limpando]$(NC)   objetos de $(OBJ_DIR)/"

fclean: clean
	@$(RM) $(NAME)
	@echo "$(RED)[Limpando]$(NC)   binario $(NAME)"

re: fclean all

test: $(NAME)
	@echo "$(BLUE)[Testando]$(NC)   curl-suite"
	@bash tests/scripts/curl-suite.sh

.PHONY: all clean fclean re test

-include $(DEPS)
