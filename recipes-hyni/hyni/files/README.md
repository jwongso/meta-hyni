# hyni_gc
**Hyni** - Dynamic, schema-based context management for chat APIs

*Write once, run anywhere. Switch between OpenAI, Claude, DeepSeek, and more with zero code changes.*

---

## ✨ Why Hyni?

```cpp
// One line to rule them all - works with ANY LLM provider
#include "schema_registry.h"
#include "context_factory.h"
#include "chat_api.h"

using namespace hyni;

// Create factory once and reuse
auto registry = schema_registry::create()
                   .set_schema_directory("./schemas")
                   .build();
auto factory = std::make_shared<context_factory>(registry);

// Create a chat API for any provider
auto chat = chat_api_builder()
               .with_schema("schemas/claude.json")
               .with_api_key(api_key)
               .with_config({.default_temperature = 0.3, .default_max_tokens = 500})
               .build();

// Set system message and send your query
chat->get_context().set_system_message("You are a helpful professor in quantum theory");
std::string answer = chat->send_message("What is quantum mechanics?");
```

**No more provider-specific code.** No more JSON wrestling. Just pure, elegant AI conversations.

---

## 🚀 Features

- 🎯 **Provider Agnostic** - Switch between OpenAI, Claude, DeepSeek with one line
- 🧠 **Smart Context Management** - Automatic conversation history and memory
- 🎙️ **Live Audio Transcription** - Built-in Whisper integration (TODO)
- 💬 **Streaming & Async** - Real-time responses with full async support
- 🛑 **Cancellation Control** - Stop requests mid-flight
- 📦 **Modern C++20** - Clean, expressive API design
- 🔧 **Schema-Driven** - JSON configs handle all provider differences
- 🖥️ **Qt6 GUI Application** - Full-featured desktop chat interface

---

## 🎯 Quick Start

### The Simplest Way
```cpp
#include "general_context.h"
#include "chat_api.h"
using namespace hyni;

// Create a context and chat API
auto context = std::make_unique<general_context>("schemas/claude.json");
chat_api chat(std::move(context));
chat.get_context().set_api_key("XYZ").set_system_message("You are a friendly software engineer");

// One-liner conversation
std::string answer = chat.send_message("What is recursion?");
```

### Type-Safe Builder Pattern
```cpp
// Use the builder for compile-time safety
auto chat = chat_api_builder<>()
                .with_schema("schemas/openai.json")  // Required first!
                .with_api_key("your-api-key")        // Optional, any order
                .with_config({                       // Optional, any order
                    .default_temperature = 0.8,
                    .default_max_tokens = 1000
                })
                .build();

std::string response = chat->send_message("What's the difference between a macchiato and a cortado?");
```

### Thread-Local Context for Multi-Threaded Apps
```cpp
// Create factory once at application startup
auto registry = schema_registry::create()
                   .set_schema_directory("./schemas")
                   .build();
auto factory = std::make_shared<context_factory>(registry);

// In each worker thread
void process_query(std::shared_ptr<context_factory> factory, const std::string& query) {
    // Get thread-local context - created once per thread
    auto& context = factory->get_thread_local_context("claude");
    context.set_api_key(get_api_key());
    
    chat_api chat(&context);  // Uses reference to thread-local context
    std::string response = chat.send_message(query);
    
    // Process response...
}
```

### Reuse the same chat for multiple or multi-turn questions
```cpp
std::string response = chat.send_message("Write a C++ class for a stack");
chat.get_context().add_assistant_message(response);
response = chat.send_message("Now add error handling");
chat.get_context().add_assistant_message(response);
response = chat.send_message("Show me how to test it");
chat.get_context().add_assistant_message(response);
response = chat.send_message("And now, explain me the time and space complexities");
```

### Advanced Context Management
```cpp
auto context = std::make_unique<general_context>("schemas/claude.json");
context->set_system_message("You are an expert in German tax system");

// Build conversations naturally
context->add_user_message("Can you help me?");
context->add_assistant_message("I'd be happy to help! What's your question?");
context->add_user_message("What is the difference between child allowance and child benefit in tax matters?");

chat_api chat(std::move(context));
auto response = chat.send_message(); // Send with existing context
```

### Multimodal Conversations with Images

```cpp
#include "chat_api.h"
#include "chat_api_builder.h"

using namespace hyni;

// Create a context with a provider that supports multimodal inputs
auto chat = chat_api_builder<>()
                .with_schema("schemas/claude.json")  // Claude 3 supports images
                .with_api_key("your-api-key")
                .build();

// Method 1: Direct file path
chat->get_context().add_user_message(
    "What can you tell me about this image?", 
    "image/png", 
    "path/to/your/image.png"
);

// Method 2: Base64-encoded image data
std::string base64_data = read_base64_from_file("encoded_image.txt");
chat->get_context().add_user_message(
    "Describe this chart in detail and explain what it means.", 
    "image/jpeg", 
    base64_data
);

// Method 3: With streaming response for multimodal
chat->send_message_stream(
    "What text appears in this image?",
    "image/png",
    "screenshots/error_message.png",
    [](const std::string& chunk) {
        std::cout << chunk << std::flush;
        return true;  // Continue streaming
    }
);

// Method 4: Multiple images in one conversation
auto& context = chat->get_context();
context.add_user_message("I'll show you two images. Compare them.");
context.add_user_message("Here's the first one:", "image/png", "product_v1.png");
context.add_assistant_message("I see the first image. It shows a product with...");
context.add_user_message("Here's the second one:", "image/png", "product_v2.png");

std::string comparison = chat->send_message("What are the key differences?");
```
---
## 🖥️ Hyni GUI Application
Hyni includes a full-featured Qt6-based desktop application for interacting with LLM providers through a modern graphical interface.

### GUI Features
- 🎨 **Modern Qt6 Interface** - Clean, responsive design with markdown rendering
- 🔄 **Dynamic Provider Loading** - Automatically discovers and loads provider schemas
- 🔑 **Smart API Key Management** - Loads from environment variables or ~/.hynirc
- 💬 **Multi-turn Conversations** - Maintain context across multiple messages
- 🌊 **Streaming Support** - Real-time response streaming with visual feedback
- 📝 **Markdown Rendering** - Beautiful formatting for code and structured content
- 🎯 **System Messages** - Set custom instructions for AI behavior
- 🔍 **Debug Information** - Comprehensive debugging and status information

### Building the GUI
```cpp
# Prerequisites
# - Qt6 (Core, Widgets, Network)
# - nlohmann_json
# - C++20 compiler

# Build with CMake
mkdir build && cd build
cmake .. -DBUILD_GUI=ON
make -j$(nproc)

# Run the GUI
./ui/hyni_gui
```

### GUI Dependencies
Add to your system or use package manager:
```bash
# Ubuntu/Debian
sudo apt install qt6-base-dev qt6-tools-dev nlohmann-json3-dev

# Fedora
sudo dnf install qt6-qtbase-devel qt6-qttools-devel json-devel

# macOS (with Homebrew)
brew install qt6 nlohmann-json

# Windows (with vcpkg)
vcpkg install qt6-base qt6-tools nlohmann-json
```

### Using the GUI
1. Launch the Application

```bash
./hyni_gui
```

2. Select Schema Directory
- File → Select Schema Directory
- Choose the folder containing your provider JSON schemas

3. Configure API Keys
The GUI automatically loads API keys from:
- Environment variables (OA_API_KEY, CL_API_KEY, DS_API_KEY, MS_API_KEY)
- ~/.hynirc file
- Or set manually via Settings → Set API Key
Example ~/.hynirc:
```bash
export OA_API_KEY="sk-..."
export CL_API_KEY="sk-ant-..."
export DS_API_KEY="sk-..."
export MS_API_KEY="..."
```

4. Select Provider
- Use the Provider menu to switch between loaded providers
- Providers with configured API keys show a checkmark ✓

5. Chat Features
- Streaming: Toggle real-time response streaming
- Multi-turn: Keep conversation history for context
- Markdown: Enable/disable markdown rendering
- System Message: Set via Settings → Set System Message
- Model Selection: Choose model from status bar dropdown

6. Keyboard Shortcuts
- Ctrl+Enter: Send message
- Ctrl+L: Clear conversation
- Ctrl+K: Set API key
- Ctrl+M: Set system message
- Ctrl+D: Show debug information
- Ctrl+R: Reload API keys
- Ctrl+Shift+R: Reload schemas

### GUI Architecture
The GUI is built with clean separation of concerns:
```
MainWindow
    ├── ChatWidget (conversation display and input)
    │   ├── QTextBrowser (markdown-capable display)
    │   ├── QTextEdit (user input)
    │   └── Control checkboxes (streaming, multi-turn, markdown)
    ├── ProviderManager (schema and provider management)
    ├── SchemaLoader (threaded JSON schema loading)
    └── ApiWorker (threaded API communication)
        ├── StreamingApiWorker
        └── NonStreamingApiWorker
```

### Customizing the GUI
The GUI supports custom styling through Qt stylesheets:

```cpp
// In chat_widget.cpp
m_inputText->setStyleSheet(R"(
    QTextEdit {
        background-color: #ffffff;
        color: #000000;
        border: 1px solid #ccc;
        border-radius: 4px;
        padding: 5px;
    }
)");
```

### GUI Configuration
Settings are persisted using QSettings:
- Schema directory location
- Window geometry and state
- User preferences

---

## 🔄 Sync vs Async - Your Choice

### Synchronous (Simple)
```cpp
auto context = std::make_unique<general_context>("schemas/deepseek.json");
chat_api chat(std::move(context));

std::string result = chat.send_message("Explain how to breed kois");
std::cout << result << std::endl;
```

### Asynchronous (Powerful)
```cpp
auto context = std::make_unique<general_context>("schemas/claude.json");
chat_api chat(std::move(context));

auto future = chat.send_message_async("Generate a story about AI");

// Do other work...
std::this_thread::sleep_for(std::chrono::seconds(1));

// Get result when ready
std::string story = future.get();
```

### Streaming (Real-time)
```cpp
auto context = std::make_unique<general_context>("schemas/openai.json");
chat_api chat(std::move(context));

chat.send_message_stream("Write a long technical article",
    [](const std::string& chunk) {
        std::cout << chunk << std::flush;  // Print as it arrives
        return true;  // Continue streaming
    },
    [](const std::string& final_response) {
        std::cout << "\n--- Complete! ---\n";
    });
```

### Cancellable Operations
```cpp
std::atomic<bool> should_cancel{false};

auto context = std::make_unique<general_context>("schemas/openai.json");
chat_api chat(std::move(context));

auto future = chat.send_message_async("This might take a while...");

// Cancel check function
auto cancel_check = [&should_cancel]() { 
    return should_cancel.load(); 
};

// Cancel after 5 seconds
std::this_thread::sleep_for(std::chrono::seconds(5));
should_cancel = true;
```

---

## 🎨 Multiple Ways to Build Conversations

### 1. Direct Message Sending
```cpp
auto context = std::make_unique<general_context>("schemas/claude.json");
context->set_system_message("You are a senior C++ developer");

chat_api chat(std::move(context));
auto response = chat.send_message("Implement quicksort in C++");
```

### 2. Context-First Approach (Recommended)
```cpp
auto context = std::make_unique<general_context>("schemas/openai.json");
context->set_system_message("You are a creative writer");
context->add_user_message("Write a haiku about programming");

chat_api chat(std::move(context));
std::string poem = chat.send_message(); // Uses existing context
```

### 3. Conversational Building
```cpp
auto context = std::make_unique<general_context>("schemas/claude.json");
chat_api chat(std::move(context));

// Build conversation step by step
chat.get_context().add_user_message("Hello!");
chat.get_context().add_assistant_message("Hi! How can I help you?");
chat.get_context().add_user_message("Tell me about the meaning of life");

std::string response = chat.send_message();
```

### 4. Parameter Configuration
```cpp
auto context = std::make_unique<general_context>("schemas/claude.json");
context->set_parameter("temperature", 0.3);
context->set_parameter("max_tokens", 500);
context->set_parameter("top_p", 0.9);

chat_api chat(std::move(context));
std::string response = chat.send_message("Explain machine learning");
```

---

## 🔧 Provider Configuration

### Dynamic Provider Switching
```cpp
// Create contexts for different providers
auto openai_context = std::make_unique<general_context>("schemas/openai.json");
auto claude_context = std::make_unique<general_context>("schemas/claude.json");
auto deepseek_context = std::make_unique<general_context>("schemas/deepseek.json");

// Same question, different providers
std::string question = "Explain machine learning";

chat_api openai_chat(std::move(openai_context));
chat_api claude_chat(std::move(claude_context));
chat_api deepseek_chat(std::move(deepseek_context));

std::string openai_view = openai_chat.send_message(question);
std::string claude_view = claude_chat.send_message(question);
std::string deepseek_view = deepseek_chat.send_message(question);
```

### Using the Registry for Provider Management
```cpp
// Create registry once
auto registry = schema_registry::create()
                   .set_schema_directory("./schemas")
                   .build();
auto factory = std::make_shared<context_factory>(registry);

// Get available providers
auto providers = registry->get_available_providers();
std::cout << "Available providers: " << std::endl;
for (const auto& provider : providers) {
    std::cout << "- " << provider << std::endl;
}

// Create context for any available provider
auto context = factory->create_context("claude");
chat_api chat(std::move(context));

std::string response = chat.send_message("How do I design a scalable API?");
```

---

## 📐 Architecture

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   Your Code     │──▶│  Hyni Context    │──▶│  LLM Provider   │
│                 │    │  Management      │    │  (Any/All)      │
└─────────────────┘    └──────────────────┘    └─────────────────┘
         │                       │                       │
         │              ┌────────▼────────┐              │
         │              │ Schema Registry │              │
         │              │ (JSON Config)   │              │
         │              └─────────────────┘              │
         │                       │                       │
         │              ┌────────▼────────┐              │
         │              │ Context Factory │              │
         │              └─────────────────┘              │
         │                                               │
         ▼                                               ▼
┌─────────────────┐                           ┌─────────────────┐
│ Thread-Local    │                           │   Streaming     │
│ Contexts        │                           │   Response      │
└─────────────────┘                           └─────────────────┘
         │                                               │
         ▼                                               ▼
┌─────────────────┐                           ┌─────────────────┐
│   Qt6 GUI       │                           │   CLI Tools     │
│   Application   │                           │   (Optional)    │
└─────────────────┘                           └─────────────────┘
```

### Core Components

- **`general_context`** - Smart conversation management with automatic history
- **`chat_api`** - Provider-agnostic HTTP client with streaming support
- **`prompt`** - Type-safe message construction with validation
- **`schema_engine`** - JSON-driven provider configuration
- **`chat_api_builder`** - Type-safe builder with compile-time validation
- **`hyni_gui`** - Qt6-based graphical user interface

---

## 🔄 Advanced Features

### Thread-Local Context with Provider Helper
```cpp
// Create once at application startup
auto registry = schema_registry::create()
                   .set_schema_directory("./schemas")
                   .build();
auto factory = std::make_shared<context_factory>(registry);

// Create provider context helpers
provider_context openai_ctx(factory, "openai");
provider_context claude_ctx(factory, "claude");

// In any thread:
void process_request(const std::string& query) {
    // Get thread-local context for claude
    auto& context = claude_ctx.get();
    context.set_api_key(get_api_key());
    
    chat_api chat(&context);
    std::string response = chat.send_message(query);
    
    // Process response...
}
```

### Conversation State Management
```cpp
auto context = std::make_unique<general_context>("schemas/claude.json");
chat_api chat(std::move(context));

// Build up conversation history
chat.send_message("What are the SOLID principles?");
chat.send_message("Can you give me an example of the Single Responsibility Principle?");
chat.send_message("How does this apply to C++ classes?");

// Export conversation state for persistence
nlohmann::json state = chat.get_context().export_state();
save_to_file("conversation.json", state.dump());

// Later, import the state to continue
auto new_context = std::make_unique<general_context>("schemas/claude.json");
nlohmann::json saved_state = nlohmann::json::parse(read_from_file("conversation.json"));
new_context->import_state(saved_state);

chat_api new_chat(std::move(new_context));
std::string response = new_chat.send_message("Can we apply this to microservices?");
```

### Stream Processing with Completion Callbacks
```cpp
auto context = std::make_unique<general_context>("schemas/claude.json");
chat_api chat(std::move(context));

std::string accumulated_response;

chat.send_message_stream("Write a detailed explanation of RAII",
    // Chunk callback - called for each piece of response
    [&accumulated_response](const std::string& chunk) {
        std::cout << chunk << std::flush;
        accumulated_response += chunk;
        return true; // Continue streaming
    },
    // Completion callback - called when stream is complete
    [&accumulated_response](const std::string& final_response) {
        std::cout << "\n\n--- Stream Complete ---\n";
        std::cout << "Total length: " << final_response.length() << " characters\n";
        
        // Save to file, log, or process final response
        save_to_file("raii_explanation.txt", final_response);
    });
```

### Error Handling and Cancellation
```cpp
auto context = std::make_unique<general_context>("schemas/claude.json");
chat_api chat(std::move(context));

std::atomic<bool> user_cancelled{false};

try {
    auto future = chat.send_message_async("Generate a very long story...");
    
    // Simulate user cancellation after 3 seconds
    std::this_thread::sleep_for(std::chrono::seconds(3));
    user_cancelled = true;
    
    std::string result = future.get();
    
} catch (const std::exception& e) {
    std::cerr << "Request failed or was cancelled: " << e.what() << std::endl;
}
```

---

## 🛠️ Error Handling

```cpp
#include <stdexcept>

auto context = std::make_unique<general_context>("schemas/claude.json");
chat_api chat(std::move(context));

try {
    std::string response = chat.send_message("Hello world");
    std::cout << response << std::endl;
    
} catch (const schema_exception& e) {
    std::cerr << "Schema error: " << e.what() << std::endl;
} catch (const validation_exception& e) {
    std::cerr << "Validation error: " << e.what() << std::endl;
} catch (const std::runtime_error& e) {
    std::cerr << "Runtime error: " << e.what() << std::endl;
} catch (const std::exception&  e) {
    std::cerr << "General error: " << e.what() << std::endl;
}

// With streaming and cancellation
std::atomic<bool> should_stop{false};

chat.send_message_stream("Long generation task...",
    [](const std::string& chunk) {
        std::cout << chunk;
        return true; // Continue
    },
    [](const std::string& final) {
        std::cout << "\nGeneration complete!\n";
    },
    [&should_stop]() {
        return should_stop.load(); // Check for cancellation
    });
```

---

## 📋 Installation

```bash
# CMake
find_package(hyni REQUIRED)
target_link_libraries(your_target hyni::hyni)

# vcpkg
vcpkg install hyni

# Conan
conan install hyni/1.0@
```

---

## Dependencies

### Core Library
- C++20 compatible compiler
- nlohmann_json (≥ 3.11)
- libcurl or similar HTTP client library
- CMake (≥ 3.16)
### GUI Application (optional)
- Qt6 (Core, Widgets, Network modules)
- All core library dependencies

---

## 🤝 Contributing

We love contributions! Whether it's new LLM providers, features, or bug fixes.

```bash
git clone https://github.com/your-org/hyni_gc.git
cd hyni_gc
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Adding a New Provider
1. Create a JSON schema in schemas/ directory
2. Test with the core library
3. Verify in the GUI application
4. Submit a pull request

---

## 📄 License

MIT License - Use freely in commercial and open-source projects.

---

**Ready to supercharge your AI conversations?** 

```cpp
#include "chat_api.h"
#include "chat_api_builder.h"

using namespace hyni;

auto chat = chat_api_builder<>()
                .with_schema("schemas/claude.json")
                .with_api_key("your-api-key")
                .build();

auto response = chat->send_message("How do I get started with Hyni?");
// "Just create a context with a schema, build a chat_api, and start chatting!"
```

Generated by Claude Sonnet 4
