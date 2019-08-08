#pragma once
#include <stdio.h>
#include <glad\glad.h>
#include <GLFW\glfw3.h>

#include "loguru.hpp"

#include "Shader.hpp"
#include "Mesh.hpp"
#include "vdf.hpp"
#include <vector>

#include <filesystem>

#include "stb_image.h"

#define EXTRATYPE_STRING 8989124

/* OpenGL compositor frame */

namespace TARCF {
	// Full screen quad for drawing to screen
	Mesh* s_mesh_quad;
	Shader* s_debug_shader;

	// Collection of shaders for nodes
	namespace SHADERLIB {
		Shader* passthrough;
		Shader* distance;

		std::map<std::string, Shader*> node_shaders;
	}

	// Collection of textures
	namespace TEXTURELIB {

	}

	class BaseNode;

	class Node;
	class NodeInstance;
	std::map<std::string, BaseNode*> NODELIB = {};

	// Property struct
	struct prop {
		GLenum type; // type of data
		unsigned int dsize; // size of datatype
		void* value = NULL;	// Arbitrary data
		int uniformloc; // Location of the uniform in shader
		
		// Sets this properties value
		void setValue(void* src) {
			if(type != EXTRATYPE_STRING) memcpy(value, src, dsize); // copy value in
			else {
				dsize = strlen((char*)src) + 1; // recalculate size
				free( value );   // delete old string
				value = malloc( dsize ); // alloc new memory
				memcpy(value, src, dsize); // copy in new value
			}
		}

		// Default constructor
		prop() {}

		// Constructor
		prop(GLenum eDataType, void* src, int uniformLocation = 0):
			type(eDataType), uniformloc(uniformLocation)
		{
			switch (eDataType) {
			case GL_FLOAT: dsize = sizeof(float); break;
			case GL_FLOAT_VEC2: dsize = sizeof(float) * 2; break;
			case GL_FLOAT_VEC3: dsize = sizeof(float) * 3; break;
			case GL_FLOAT_VEC4: dsize = sizeof(float) * 4; break;
			case GL_INT: dsize = sizeof(int); break;
			case EXTRATYPE_STRING: dsize = strlen((char*)src)+1; break;
			default: LOG_F(WARNING, "UNSUPPORTED UNIFORM TYPE: %u", eDataType); return;
			}

			LOG_F(2, "	Storage size: %u", dsize);

			value = calloc( dsize, 1 );			// alloc new storage
			if(src) memcpy(value, src, dsize);	// copy in initial value
			if (eDataType == EXTRATYPE_STRING) LOG_F(3, "strv: %s", value);
		}

		~prop() {
			LOG_F(2, "dealloc()");
			free( value ); // Clean memory created from constructor
		}

		// Copy-swap assignment
		prop& operator=(prop copy) {
			std::swap(dsize, copy.dsize);
			std::swap(type, copy.type);
			std::swap(value, copy.value);
			return *this;
		}
		
		// Move constructor
		prop(prop&& other){
			dsize = other.dsize;
			type = other.type;
			value = other.value;
			
			other.value = NULL;
			other.type = 0;
			other.dsize = 0;
		}
		
		// Copy constructor
		prop(const prop& other){
			dsize = other.dsize;
			type = other.type;
			value = malloc(dsize);
			memcpy(value, other.value, dsize);
		}
	};

	// Defines an output for a node.
	struct Pin {
		std::string name; // symbolic name for this output
		int location = -1; // Location for this uniform/output

		Pin() {}
		Pin(const std::string& _name)
			: name(_name) {}
		Pin(const std::string& _name, const int& _location)
			: name(_name), location(_location) {}
	};

	// Node class. Base class for every type of node
	class BaseNode {
	public:
		// Operator shader name
		Shader* m_operator_shader;

		// Property definitions for this node, contains default values.
		std::map<std::string, prop> m_prop_definitions;

		// List of output definitions
		std::vector<Pin> m_input_definitions;
		std::vector<Pin> m_output_definitions;

		// Constructor
		BaseNode(Shader* sOpShader) :
			m_operator_shader(sOpShader)
		{
			//LOG_F(2, "Creating node from shader ( %s )", sOpShader->symbolicName.c_str());

			int count;
			int size;
			GLenum type;
			const int buf_size = 32;
			char buf_name[buf_size];
			int name_length;

			// Extract uniforms from shader
			glGetProgramiv(sOpShader->programID, GL_ACTIVE_UNIFORMS, &count);

			// Get all uniforms
			for (int i = 0; i < count; i++) {
				glGetActiveUniform(sOpShader->programID, i, buf_size, &name_length, &size, &type, buf_name);
				if(type == GL_FLOAT || type == GL_FLOAT_VEC2 || type == GL_FLOAT_VEC3 || type == GL_FLOAT_VEC4)
				m_prop_definitions.insert({ std::string(buf_name), prop(type, NULL, i) }); // write to definitions

				// Classify samplers as inputs
				else if (type == GL_SAMPLER_2D) {
					this->m_input_definitions.push_back(Pin(buf_name, i));
				}
			}
		}

		void showInfo() const {
			LOG_F(INFO, "Inputs: %u", this->m_input_definitions.size());
			for(auto&& input: this->m_input_definitions) LOG_F(INFO, "  %i: %s", input.location, input.name.c_str());

			LOG_F(INFO, "Outputs: %u", this->m_output_definitions.size());
			for(auto&& output: this->m_output_definitions) LOG_F(INFO, "  %i: %s", output.location, output.name.c_str());

			LOG_F(INFO, "Attributes: %u", this->m_prop_definitions.size());
			for (auto&& attrib: this->m_prop_definitions) LOG_F(INFO, "  %i: %s", attrib.second.uniformloc, attrib.first.c_str());
		}

		// Compute a node's outputs
		virtual void compute(NodeInstance* node);

		// Destructor
		~BaseNode() {}

		// Virtual functions
		// Generates required texture memory for a node
		// Default implmentation: Setup single RGBA texture on channel 0.
		virtual void v_gen_tex_memory(NodeInstance* instance);

		// Creates buffers
		virtual void v_gen_buffers(NodeInstance* instance);

		// Clears node
		virtual void clear(const NodeInstance* node);

		// Draws what this node is currently storing to screen
		virtual void debug_fs(const NodeInstance* instance, int channel = 0);
	};

	// Standard node.
	class Node: public BaseNode {
	public:
		Node(Shader* sOpShader):
			BaseNode(sOpShader)
		{
			
		}
	};

	// Bidirection connection struct
	struct Connection {
		NodeInstance* ptrNode; // Connected node
		unsigned int uConID; // target connection ID

		Connection() {}

		Connection(NodeInstance* _ptrNode, const unsigned int& _uConID)
			: ptrNode(_ptrNode),
			uConID(_uConID) {}
	};

#define MAX_CHANNELS 16

	// This is an instance of a node which holds property values.
	class NodeInstance {
	public:
		// Keep track whether this node needs to be calculated
		bool m_isDirty = true;

		// Connections to this node
		std::vector<std::vector<Connection>> m_con_outputs;	// Output connections: this['o_pos][<i>] -> ptrNode[uConID]
		std::vector<Connection> m_con_inputs;								// Inputs: ptrNode[uConID] -> this['i_pos]

		// Internal texture storage
		unsigned int m_gl_texture_ids[MAX_CHANNELS] = { 0 };
		unsigned int m_gl_texture_w;
		unsigned int m_gl_texture_h;
		unsigned int m_gl_framebuffers[MAX_CHANNELS] = { 0 };

		// Associated master node
		std::string m_nodeid;

		// Local instance properties
		std::map<std::string, prop> m_properties;

		// Check for framebuffer completeness
		inline bool check_buffer() {
			return (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
		}

		// Constructor creates framebuffer and texture memory, sets up properties
		NodeInstance(const unsigned int& iWidth, const unsigned int& iHeight, const std::string& sNodeId) :
			m_gl_texture_w(iWidth), m_gl_texture_h(iHeight), m_nodeid(sNodeId)
		{
			NODELIB[m_nodeid]->v_gen_buffers(this);

			// Copy properties
			for(auto&& p: NODELIB[m_nodeid]->m_prop_definitions){
				this->m_properties.insert({ p.first, prop(p.second) });
			}

			// Copy in/outs as connections
			for(auto&& defIn: NODELIB[m_nodeid]->m_input_definitions){
				this->m_con_inputs.push_back(Connection(NULL, 0)); // initialize as connected to nothing
			}

			// Copy outputs as connections
			for (auto&& defOut: NODELIB[m_nodeid]->m_output_definitions) {
				this->m_con_outputs.push_back(std::vector<Connection>{}); // create empty output connections
			}

			// Generate texture memory for this node
			NODELIB[m_nodeid]->v_gen_tex_memory(this);
			if (!this->check_buffer()) LOG_F(ERROR, "(NODE) Framebuffer did not complete");

		}

		// Destructor deallocates texture memory and framebuffer
		~NodeInstance() {
			LOG_F(2, "Deallocating node storage ( type:%s )", m_nodeid.c_str());

			// Delete texture storage.
			for (auto&& uTex : m_gl_texture_ids)
				if (uTex) glDeleteTextures(1, &uTex);

			unsigned int id = 0;
			for(auto&& buf: m_gl_framebuffers) if(buf) glDeleteFramebuffers(1, &this->m_gl_framebuffers[id++]);
			
		}

		// Call respective compute function
		void compute() { 
			// Compute any dependent input nodes if they are dirty
			for(auto&& input: this->m_con_inputs)
				if (input.ptrNode) if (input.ptrNode->m_isDirty) input.ptrNode->compute();

			// Pull inputs
			unsigned int inputnum = 0;
			for(auto&& input: this->m_con_inputs){
				if (input.ptrNode) {
					glActiveTexture(GL_TEXTURE0 + inputnum++);
					glBindTexture(GL_TEXTURE_2D, input.ptrNode->m_gl_texture_ids[0]);
				}
			}
			glActiveTexture(GL_TEXTURE0);
			LOG_F(INFO, "Computing node type: %s", this->m_nodeid.c_str());

			// Compute this node
			NODELIB[this->m_nodeid]->compute(this);

			// Mark this as done
			this->m_isDirty = false;
			
		}

		// Call respective debug_fs function
		inline void debug_fs() { NODELIB[m_nodeid]->debug_fs(this); }

		// Set a property
		void setProperty(const std::string& propname, void* ptr) {
			if (m_properties.count(propname)) m_properties[propname].setValue(ptr);
		}

		// Connects two nodes together
		static void connect(NodeInstance* src, NodeInstance* dst, const unsigned int& conSrcID, const unsigned int& conDstID) {
			src->m_con_outputs[conSrcID].push_back( Connection(dst, conDstID) );
			dst->m_con_inputs[conDstID] = Connection(src, conSrcID);
		}
	};

	

	void BaseNode::compute(NodeInstance* node) {
		glViewport(0, 0, node->m_gl_texture_w, node->m_gl_texture_h);
		glBindFramebuffer(GL_FRAMEBUFFER, node->m_gl_framebuffers[0]);
		this->m_operator_shader->use();
		LOG_F(INFO, "Shaderr: %s", this->m_operator_shader->symbolicName.c_str());
		s_mesh_quad->Draw();
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void BaseNode::v_gen_buffers(NodeInstance* instance) {
		// Generate framebuffer
		glGenFramebuffers(1, &instance->m_gl_framebuffers[0]);
		glBindFramebuffer(GL_FRAMEBUFFER, instance->m_gl_framebuffers[0]);
	}

	// Generic tex mem handler
	void BaseNode::v_gen_tex_memory(NodeInstance* instance) {
		unsigned int id = 0;
		unsigned int* attachments = new unsigned int[this->m_output_definitions.size()];
		for (auto&& texOut: this->m_output_definitions) {
			glGenTextures(1, &instance->m_gl_texture_ids[id]);
			glBindTexture(GL_TEXTURE_2D, instance->m_gl_texture_ids[id]);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, instance->m_gl_texture_w, instance->m_gl_texture_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+id, GL_TEXTURE_2D, instance->m_gl_texture_ids[id], 0);
			attachments[id] = GL_COLOR_ATTACHMENT0 + id;
			id++;
		}
		glDrawBuffers(this->m_output_definitions.size(), attachments);
		delete[] attachments;
	}

	void BaseNode::clear(const NodeInstance* node) {
		glViewport(0, 0, node->m_gl_texture_w, node->m_gl_texture_h);
		glBindFramebuffer(GL_FRAMEBUFFER, node->m_gl_framebuffers[0]);
		glClearColor(0.0, 0.5, 0.5, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void BaseNode::debug_fs(const NodeInstance* instance, int channel) {
		s_debug_shader->use();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, instance->m_gl_texture_ids[channel]);
		s_mesh_quad->Draw();
	}

	namespace Atomic {
		// Node that loads texture from disk
		class TextureNode: public BaseNode {
		public:
			// Constructor sets string property to path
			TextureNode(const std::string& sSource):
				BaseNode(SHADERLIB::passthrough)
			{
				m_prop_definitions.insert({ "source", prop(EXTRATYPE_STRING, (void*)sSource.c_str()) });
				m_output_definitions.push_back(Pin("output", 0));
			}

			void compute(NodeInstance* node) override {
				if (node->m_gl_texture_ids[0]) glDeleteTextures(1, &node->m_gl_texture_ids[0]); // delete original texture
			
				// Load texture via gen-tex
				v_gen_tex_memory(node);
			}

			// Override texture mem generation so we can load from disk instead.
			void v_gen_tex_memory(NodeInstance* instance) override {
				glBindFramebuffer(GL_FRAMEBUFFER, instance->m_gl_framebuffers[0]); // bind framebuffer
				glGenTextures(1, &instance->m_gl_texture_ids[0]);
				glBindTexture(GL_TEXTURE_2D, instance->m_gl_texture_ids[0]);

				// Load image via stb
				int width, height, nrChannels;
				stbi_set_flip_vertically_on_load(true);

				LOG_F(3, "Opening image: %s", (char*)instance->m_properties["source"].value);

				unsigned char* data = stbi_load((char*)instance->m_properties["source"].value, &width, &height, &nrChannels, 4);

				if (data) {
					instance->m_gl_texture_w = width;
					instance->m_gl_texture_h = height;
				} 

				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, instance->m_gl_texture_w, instance->m_gl_texture_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
			
				free( data );

				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, instance->m_gl_texture_ids[0], 0);

				unsigned int attachments[1] = {
					GL_COLOR_ATTACHMENT0
				};

				glDrawBuffers(1, attachments);
			}
		};

		// Node that measures distance to nearest 'landmass'
		class Distance: public BaseNode {
		public:
			Distance() :
				BaseNode(SHADERLIB::distance)
			{
				m_prop_definitions.insert({ "maxdist", prop(GL_INT, 0, -1) });
				int v = 255;
				this->m_prop_definitions["maxdist"].setValue(&v);

				m_output_definitions.push_back(Pin("output", 0));
				m_input_definitions.push_back(Pin("input", 0));
			}

			void compute(NodeInstance* node) override {
				glViewport(0, 0, node->m_gl_texture_w, node->m_gl_texture_h);
				// Bind shader
				NODELIB[node->m_nodeid]->m_operator_shader->use();
				s_mesh_quad->Draw();
				for (int i = 0; i < 255; i++) {
					glBindFramebuffer(GL_FRAMEBUFFER, node->m_gl_framebuffers[i%2]);
					if(i > 0) glBindTexture(GL_TEXTURE_2D, node->m_gl_texture_ids[(i + 1) % 2]);
					NODELIB[node->m_nodeid]->m_operator_shader->setFloat("iter", (255.0f - (float)i) * 0.00392156862f);
					s_mesh_quad->Draw();
				}

				glBindFramebuffer(GL_FRAMEBUFFER, 0);
			}

			void v_gen_buffers(NodeInstance* instance) override {
				// Front and back buffer
				glGenFramebuffers(2, &instance->m_gl_framebuffers[0]);
				glBindFramebuffer(GL_FRAMEBUFFER, instance->m_gl_framebuffers[0]);
			}

			void v_gen_tex_memory(NodeInstance* instance) override {
				// BACK BUFFER
				glBindFramebuffer(GL_FRAMEBUFFER, instance->m_gl_framebuffers[0]); // bind framebuffer
				glGenTextures(1, &instance->m_gl_texture_ids[0]);
				glBindTexture(GL_TEXTURE_2D, instance->m_gl_texture_ids[0]);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, instance->m_gl_texture_w, instance->m_gl_texture_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 , GL_TEXTURE_2D, instance->m_gl_texture_ids[0], 0);
				
				unsigned int attachments[1] = {
					GL_COLOR_ATTACHMENT0
				};

				glDrawBuffers(1, attachments);

				// FRONT BUFFER
				
				glBindFramebuffer(GL_FRAMEBUFFER, instance->m_gl_framebuffers[1]);
				glGenTextures(1, &instance->m_gl_texture_ids[1]);
				glBindTexture(GL_TEXTURE_2D, instance->m_gl_texture_ids[1]);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, instance->m_gl_texture_w, instance->m_gl_texture_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, instance->m_gl_texture_ids[1], 0);
				
				glDrawBuffers(1, attachments);
			}
		};
	}

	// Init system
	void init() {
		s_mesh_quad = new Mesh({
			-1.0, -1.0, 0.0, 0.0, // bottom left
			1.0, -1.0, 1.0, 0.0, // bottom right
			1.0, 1.0, 1.0, 1.0, // top right

			-1.0, -1.0, 0.0, 0.0, // bottom left
			1.0, 1.0, 1.0, 1.0, // top right
			-1.0, 1.0, 0.0, 1.0  // top left
			}, MeshMode::POS_XY_TEXOORD_UV);

		s_debug_shader = new Shader("shaders/engine/quadbase.vs", "shaders/engine/node.preview.fs", "shader.node.preview");

		SHADERLIB::passthrough = new Shader("shaders/engine/quadbase.vs", "shaders/engine/tarcfnode/passthrough.fs", "tarcfn.passthrough");
		SHADERLIB::distance = new Shader("shaders/engine/quadbase.vs", "shaders/engine/tarcfnode/distance.fs", "tarcfn.distance");

		// Generative nodes (static custom handle nodes)
		NODELIB.insert({ "texture", new Atomic::TextureNode("textures/modulate.png") });
		NODELIB.insert({ "distance", new Atomic::Distance() });

		// Load generic transformative nodes
		for(const auto& entry: std::filesystem::directory_iterator("tarcfnode")){
			std::ifstream ifs(entry.path().c_str());
			if (!ifs) throw std::exception("Node info read error");

			std::string file_str((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
			kv::FileData file_kv(file_str);

			kv::DataBlock* block_info = file_kv.headNode->GetFirstByName("info");
			kv::DataBlock* block_shader = file_kv.headNode->GetFirstByName("shader");

			if (!block_info) { LOG_F(ERROR, "No info block in node: %s", entry.path().filename().c_str()); continue; };
			if (!block_shader) { LOG_F(ERROR, "No shader block in node: %s", entry.path().filename().c_str()); continue; };

			// Create node
			Node* ptrNodeNew = new Node(
				new Shader(
					kv::tryGetStringValue(block_shader->Values, "vertex", "shaders/engine/quadbase.vs"),
					kv::tryGetStringValue(block_shader->Values, "fragment", "shaders/engine/tarcfnode/passthrough.fs"),
					"tarcfn::" + kv::tryGetStringValue(block_info->Values, "name", "none")
				)
			);

			// Add outputs
			kv::DataBlock* block_shader_outputs = block_shader->GetFirstByName("outputs");
			if (block_shader_outputs) { // Loop output listings and add
				unsigned int autoNames = 0;
				for(auto&& outputDef: block_shader_outputs->GetAllByName("output")){
					ptrNodeNew->m_output_definitions.push_back(Pin(kv::tryGetStringValue(outputDef->Values, "name", "output_" + std::to_string(autoNames)), autoNames++));
				}
			}
			if(ptrNodeNew->m_output_definitions.size() == 0){ // Just use default outputs (1, named output)
				ptrNodeNew->m_output_definitions.push_back(Pin("output", 0));
			}

			ptrNodeNew->showInfo();

			// Create index listing.
			NODELIB.insert(
				{
				split(entry.path().filename().string(), ".tcfn")[0],
				ptrNodeNew
				}
			);
		}
	}
}

/* Universal nodes that make up other things */

// Loads a texture from a file as a node
namespace TARCF{ 

}