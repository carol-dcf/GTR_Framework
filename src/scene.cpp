#include "scene.h"
#include "utils.h"
#include <stdlib.h>     /* srand, rand */

#include "prefab.h"
#include "extra/cJSON.h"
#include "application.h"
#include "shader.h"

GTR::Scene* GTR::Scene::instance = NULL;

GTR::Scene::Scene()
{
	instance = this;

}

void GTR::Scene::clear()
{
	for (int i = 0; i < entities.size(); ++i)
	{
		BaseEntity* ent = entities[i];
		delete ent;
	}
	entities.resize(0);
}

void GTR::Scene::addEntity(BaseEntity* entity)
{
	entities.push_back(entity); entity->scene = this;
}

bool GTR::Scene::load(const char* filename)
{
	std::string content;

	this->filename = filename;
	std::cout << " + Reading scene JSON: " << filename << "..." << std::endl;

	if (!readFile(filename, content))
	{
		std::cout << "- ERROR: Scene file not found: " << filename << std::endl;
		return false;
	}

	//parse json string 
	cJSON* json = cJSON_Parse(content.c_str());
	if (!json)
	{
		std::cout << "ERROR: Scene JSON has errors: " << filename << std::endl;
		return false;
	}

	//read global properties
	background_color = readJSONVector3(json, "background_color", background_color);
	ambient_light = readJSONVector3(json, "ambient_light", ambient_light);
	main_camera.eye = readJSONVector3(json, "camera_position", main_camera.eye);
	main_camera.center = readJSONVector3(json, "camera_target", main_camera.center);
	main_camera.fov = readJSONNumber(json, "camera_fov", main_camera.fov);

	//entities
	cJSON* entities_json = cJSON_GetObjectItemCaseSensitive(json, "entities");
	cJSON* entity_json;
	cJSON_ArrayForEach(entity_json, entities_json)
	{
		std::string type_str = cJSON_GetObjectItem(entity_json, "type")->valuestring;
		BaseEntity* ent = createEntity(type_str);
		if (!ent)
		{
			std::cout << " - ENTITY TYPE UNKNOWN: " << type_str << std::endl;
			//continue;
			ent = new BaseEntity();
		}

		addEntity(ent);

		if (cJSON_GetObjectItem(entity_json, "name"))
		{
			ent->name = cJSON_GetObjectItem(entity_json, "name")->valuestring;
			stdlog(std::string(" + entity: ") + ent->name);
		}

		//read transform
		if (cJSON_GetObjectItem(entity_json, "position"))
		{
			ent->model.setIdentity();
			Vector3 position = readJSONVector3(entity_json, "position", Vector3());
			ent->model.translate(position.x, position.y, position.z);
		}

		if (cJSON_GetObjectItem(entity_json, "angle"))
		{
			float angle = cJSON_GetObjectItem(entity_json, "angle")->valuedouble;
			ent->model.rotate(angle * DEG2RAD, Vector3(0, 1, 0));
		}

		if (cJSON_GetObjectItem(entity_json, "rotation"))
		{
			Vector4 rotation = readJSONVector4(entity_json, "rotation");
			Quaternion q(rotation.x, rotation.y, rotation.z, rotation.w);
			Matrix44 R;
			q.toMatrix(R);
			ent->model = R * ent->model;
		}

		if (cJSON_GetObjectItem(entity_json, "target"))
		{
			Vector3 target = readJSONVector3(entity_json, "target", Vector3());
			Vector3 front = target - ent->model.getTranslation();
			ent->model.setFrontAndOrthonormalize(front);
		}

		if (cJSON_GetObjectItem(entity_json, "scale"))
		{
			Vector3 scale = readJSONVector3(entity_json, "scale", Vector3(1, 1, 1));
			ent->model.scale(scale.x, scale.y, scale.z);
		}

		ent->configure(entity_json);
	}

	//free memory
	cJSON_Delete(json);

	for (int i = 0; i < 6; ++i)
	{
		BaseEntity* ent = createEntity("LIGHT");
		addEntity(ent);
		ent->model.setIdentity();
		ent->model.translate(-200.0 * i, 20.0, 100.0 * i);
		LightEntity* light = (GTR::LightEntity*)ent;
		light->intensity = 10.0;
		light->light_type = GTR::eLightType::POINT;
		light->max_distance = 60.0;
		light->name = "extra" + std::to_string(i);
		light->color = Vector3(1, i * 0.1, 0.3 * i);
		addEntityLight(light);
	}

	return true;
}


/////////////////////////////////////////////////
////////////////// BASE ENTITY //////////////////
/////////////////////////////////////////////////

GTR::BaseEntity* GTR::Scene::createEntity(std::string type)
{
	if (type == "PREFAB")
		return new GTR::PrefabEntity();
	if (type == "LIGHT")
		return new GTR::LightEntity();
	return NULL;
}

void GTR::BaseEntity::renderInMenu()
{
#ifndef SKIP_IMGUI
	ImGui::Text("Name: %s", name.c_str()); // Edit 3 floats representing a color
	ImGui::Checkbox("Visible", &visible); // Edit 3 floats representing a color
	//Model edit
	ImGuiMatrix44(model, "Model");
#endif
}


///////////////////////////////////////////////////
////////////////// PREFAB ENTITY //////////////////
///////////////////////////////////////////////////

GTR::PrefabEntity::PrefabEntity()
{
	entity_type = PREFAB;
	prefab = NULL;
}

void GTR::PrefabEntity::configure(cJSON* json)
{
	if (cJSON_GetObjectItem(json, "filename"))
	{
		filename = cJSON_GetObjectItem(json, "filename")->valuestring;
		prefab = GTR::Prefab::Get((std::string("data/") + filename).c_str());
	}
}

void GTR::PrefabEntity::renderInMenu()
{
	BaseEntity::renderInMenu();

#ifndef SKIP_IMGUI
	ImGui::Text("filename: %s", filename.c_str()); // Edit 3 floats representing a color
	if (prefab && ImGui::TreeNode(prefab, "Prefab Info"))
	{
		prefab->root.renderInMenu();
		ImGui::TreePop();
	}
#endif
}


//////////////////////////////////////////////////
////////////////// LIGHT ENTITY //////////////////
//////////////////////////////////////////////////

GTR::LightEntity::LightEntity()
{
	entity_type = LIGHT;
	//light_camera = new Camera();
	//fbo = FBO();
	bias = 0.001;
}

void GTR::LightEntity::renderInMenu()
{
	BaseEntity::renderInMenu();

#ifndef SKIP_IMGUI
	bool changed = false;
	changed |= ImGui::Combo("Type", (int*)&this->light_type, "NOLIGHT\0POINT\0SPOT\0DIRECTIONAL", 4);
	if (changed && light_type == SPOT) {
		this->cone_angle = 45;
		this->exponent = 4.0;
	}

	ImGui::ColorEdit4("Light color", this->color.v);
	ImGui::SliderFloat("Intensity", &this->intensity, 0, 30);
	if (this->light_type == SPOT) {
		ImGui::SliderFloat("Cone Angle", &this->cone_angle, 0, 90);
		ImGui::SliderFloat("Exponent", &this->exponent, 1, 50);
		ImGui::SliderFloat("Bias", &this->bias, 0, 0.01);
	}
	if (this->light_type != DIRECTIONAL)
		ImGui::SliderFloat("Max Distance", &this->max_distance, 10, 5000);
	if (this->light_type == DIRECTIONAL)
	{
		ImGui::SliderFloat("Area Size", &this->area_size, 100, 5000);
		ImGui::SliderFloat("Bias", &this->bias, 0, 0.01);
	}

#endif
}

void GTR::LightEntity::configure(cJSON* json)
{
	this->fbo = FBO();
	this->light_camera = new Camera();
	if (cJSON_GetObjectItem(json, "color"))
	{
		Vector3 color = readJSONVector3(json, "color", Vector3());
		this->color = color;
	}

	if (cJSON_GetObjectItem(json, "intensity"))
	{
		float intensity = cJSON_GetObjectItem(json, "intensity")->valuedouble;
		this->intensity = intensity;
	}

	if (cJSON_GetObjectItem(json, "shadow_bias"))
	{
		float shadow_bias = cJSON_GetObjectItem(json, "shadow_bias")->valuedouble;
		this->bias = shadow_bias;
	}

	if (cJSON_GetObjectItem(json, "cone_exp"))
	{
		float cone_exp = cJSON_GetObjectItem(json, "cone_exp")->valuedouble;
		this->exponent = cone_exp;
	}

	if (cJSON_GetObjectItem(json, "light_type"))
	{
		std::string l_type_str = cJSON_GetObjectItem(json, "light_type")->valuestring;
		if (l_type_str == "SPOT")
			this->light_type = GTR::eLightType::SPOT;
		else if (l_type_str == "POINT")
			this->light_type = GTR::eLightType::POINT;
		else if (l_type_str == "DIRECTIONAL")
			this->light_type = GTR::eLightType::DIRECTIONAL;
		else
			this->light_type = GTR::eLightType::NOLIGHT;
	}

	if (cJSON_GetObjectItem(json, "max_dist"))
	{
		float max_distance = cJSON_GetObjectItem(json, "max_dist")->valuedouble;
		this->max_distance = max_distance;
	}

	if (cJSON_GetObjectItem(json, "cone_angle"))
	{
		float cone_angle = cJSON_GetObjectItem(json, "cone_angle")->valuedouble;
		this->cone_angle = cone_angle;
	}

	if (cJSON_GetObjectItem(json, "area_size"))
	{
		float area_size = cJSON_GetObjectItem(json, "area_size")->valuedouble;
		this->area_size = area_size;
	}

	if (cJSON_GetObjectItem(json, "exponent"))
	{
		float exponent = cJSON_GetObjectItem(json, "exponent")->valuedouble;
		this->exponent = exponent;
	}
	
	this->scene->addEntityLight(this);
}

void GTR::LightEntity::setUniforms(Shader* shader)
{
	if (this->light_type != POINT)
	{
		Texture* shadowmap = this->shadow_buffer;
		shader->setTexture("shadowmap", shadowmap, 5);
		Matrix44 shadow_proj = this->light_camera->viewprojection_matrix;
		shader->setUniform("u_shadow_viewproj", shadow_proj);
		//we will also need the shadow bias
		shader->setUniform("u_shadow_bias", this->bias);
	}

	int l_type = static_cast<int>(this->light_type);

	//pass the light data to the shader
	shader->setUniform("u_light_color", this->color);
	shader->setUniform("u_light_type", l_type);
	shader->setUniform("u_light_position", this->model.getTranslation());

	shader->setUniform("u_maxdist", this->max_distance);
	shader->setUniform("u_light_factor", this->intensity);

	Vector3 light_direction = Vector3(0, 0, 0);
	if (this->light_type == SPOT)
		light_direction = this->model.frontVector();
	else if (this->light_type == DIRECTIONAL)
		light_direction = this->model.rotateVector(Vector3(0, 0, -1));

	shader->setUniform("u_direction", light_direction);
	float angle = this->cone_angle * DEG2RAD;
	shader->setUniform("u_spotCosineCutoff", cos(angle));
	shader->setUniform("u_spotExponent", this->exponent);
}


void GTR::Scene::addEntityLight(LightEntity* entity)
{
	l_entities.push_back(entity); entity->scene = this;
}