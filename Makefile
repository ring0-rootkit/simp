TMUX_CURRENT_WINDOW := $(shell tmux display-message -p '#{window_id}')
TMUX_PANE_0 := $(shell tmux list-panes -t $(TMUX_CURRENT_WINDOW) -F '#{pane_id}' | head -n 1)
TMUX_PANE_1 := $(shell tmux list-panes -t $(TMUX_CURRENT_WINDOW) -F '#{pane_id}' | tail -n 1)

all:
	clang server.c -o server -pthread -lrt
	clang client.c -o client -pthread -lrt

run: run-server run-client

run-server:
	@tmux send-keys -t $(TMUX_PANE_0) "clear && ./server" C-m

run-client:
	@tmux send-keys -t $(TMUX_PANE_1) "clear && sleep 1 && ./client" C-m

clean:
	rm -f server client
