# Use a lightweight base image
FROM ubuntu:20.04

# Update and install runtime dependencies
RUN apt-get update && apt-get install -y \
    libpq5 \
    ca-certificates && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Add build argument for the release tag
ARG RELEASE_TAG

# Copy the release binary from the workflow
COPY server-x86_64.tar.gz /app/

# Extract the binary
RUN tar -xvf server-x86_64.tar.gz && rm server-x86_64.tar.gz

# Ensure the binary is executable
RUN chmod +x server

# Set the binary as the container's entry point
ENTRYPOINT ["/app/server"]

# Expose the application's default port
EXPOSE 8080
