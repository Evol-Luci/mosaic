#!/bin/bash
echo "Starting local server for documentation..."
# Start python HTTP server in the background
python3 -m http.server 8000 &
SERVER_PID=$!

# Wait a moment to ensure the server starts
sleep 1

# Open the documentation hub in the default browser
echo "Opening http://localhost:8000/docs/index.html"
xdg-open "http://localhost:8000/docs/index.html"

echo "Server is running on port 8000. Press Ctrl+C to stop."
wait $SERVER_PID
