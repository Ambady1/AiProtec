import socket
import threading
import datetime

# Server configuration
HOST = '127.0.0.1'
PORT = 5001
#LOG_FILE = 'chatgpt_monitor.log'

'''def log_message(message):
    """Log a message to console and file with timestamp"""
    timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    log_entry = f"[{timestamp}] {message}"
    
    with open(LOG_FILE, 'a', encoding='utf-8') as f:
        f.write(log_entry + '\n')'''

def handle_client(client_socket, address):
    """Handle a client connection"""
    #print(f"New connection from {address[0]}:{address[1]}")
    
    while True:
        try:
            # Receive data from client
            data = b""
            while True:
                chunk = client_socket.recv(1024)
                if not chunk:
                    break
                data += chunk
                if len(chunk) < 1024:
                    break
            
            if not data:
                break
            
            # Decode and log the received data
            message = data.decode('utf-8', errors='replace')
            print(message)
            #print(f"Received from {address[0]}: {message}")
            
            # Check for potentially malicious content
            suspicious_terms = ['hack', 'exploit', 'vulnerability', 'attack', 
                                'bypass', 'inject', 'steal', 'password']
            
            if any(term in message.lower() for term in suspicious_terms):
                print(f"ALERT: Potentially malicious content detected: {message}")
            
            # Send acknowledgment
            client_socket.send(b"Data received")
            
        except Exception as e:
            print(f"Error handling client {address}: {str(e)}")
            break
    
    # Close the connection
    client_socket.close()
    #print(f"Connection closed from {address[0]}:{address[1]}")

def start_server():
    """Start the server and listen for connections"""
    try:
        # Create server socket
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((HOST, PORT))
        server.listen(5)
        
        print(f"Server started on {HOST}:{PORT}")
        
        while True:
            # Accept client connections
            client_sock, address = server.accept()
            
            # Start a new thread to handle the client
            client_thread = threading.Thread(
                target=handle_client,
                args=(client_sock, address)
            )
            client_thread.daemon = True
            client_thread.start()
            
    except KeyboardInterrupt:
        print("Server shutting down...")
    except Exception as e:
        print(f"Error: {str(e)}")
    finally:
        if 'server' in locals():
            server.close()
        print("Server stopped")

if __name__ == "__main__":
    print("Starting ChatGPT monitoring server...")
    start_server()