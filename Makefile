NAME     = webserv
CXX      = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98
INCLUDES = -I./include
SRCS     = src/main.cpp \
           src/ConfigParser.cpp \
           src/HttpRequest.cpp \
           src/HttpResponse.cpp \
           src/CgiHandler.cpp \
           src/Connection.cpp \
           src/Webserv.cpp
OBJS     = $(SRCS:.cpp=.o)

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
