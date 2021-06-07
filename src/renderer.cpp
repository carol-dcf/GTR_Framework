#include "renderer.h"

#include "camera.h"
#include "shader.h"
#include "mesh.h"
#include "texture.h"
#include "prefab.h"
#include "material.h"
#include "utils.h"
#include "scene.h"
#include "extra/hdre.h"
#include <algorithm>    // std::sort

#include "application.h"


using namespace GTR;

Vector3 degamma(Vector3 color) {
	Vector3 g_color;
	g_color.x = pow(color.x, 2.2);
	g_color.y = pow(color.y, 2.2);
	g_color.z = pow(color.z, 2.2);
	return g_color;
}

GTR::RenderCall::RenderCall() {}

GTR::Renderer::Renderer()
{
	float w = Application::instance->window_width;
	float h = Application::instance->window_height;

	render_mode = GTR::eRenderMode::DEFAULT;
	pipeline_mode = GTR::ePipelineMode::FORWARD;
	gbuffers_fbo = FBO();
	gbuffers_fbo.create(w, h, 3, GL_RGBA, GL_UNSIGNED_BYTE, true);

	dithering = true;

	ssao_fbo = FBO();
	ssao_fbo.create(w, h, 1, GL_RGB);
	ssao_blur = FBO();
	ssao_blur.create(w, h);
	blur_ssao = true;

	illumination_fbo = FBO();
	illumination_fbo.create(w, h, 1, GL_RGB, GL_FLOAT, false);
	hdr = true;

	random_points = generateSpherePoints(64, 1.0, true);

	irr_fbo = FBO();
	irr_fbo.create(64, 64, 1, GL_RGB, GL_FLOAT);
	show_probe = false;
}

void Renderer::renderProbe(Vector3 pos, float size, float* coeffs)
{
	Camera* camera = Camera::current;
	Shader* shader = Shader::Get("probe");
	Mesh* mesh = Mesh::Get("data/meshes/sphere.obj", true);

	glEnable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);

	Matrix44 model;
	model.setTranslation(pos.x, pos.y, pos.z);
	model.scale(size, size, size);

	shader->enable();
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model);
	shader->setUniform3Array("u_coeffs", coeffs, 9);

	mesh->render(GL_TRIANGLES);
}

void Renderer::updateIrradianceCache(Scene* scene) 
{
	std::cout << "- Updating Irradiance Cache" << std::endl;
	computeProbeCoefficients(scene);
	uploadProbes();
}

void Renderer::defineGrid(Scene* scene) {
	//define the corners of the axis aligned grid
	// 
	//compute the vector from one corner to the other
	delta = (end_pos - start_pos);
	
	//and scale it down according to the subdivisions
	//we substract one to be sure the last probe is at end pos
	delta.x /= (dim.x - 1);
	delta.y /= (dim.y - 1);
	delta.z /= (dim.z - 1);

	probes.clear();

	//lets compute the centers
	//pay attention at the order at which we add them
	for (int z = 0; z < dim.z; ++z)
		for (int y = 0; y < dim.y; ++y)
			for (int x = 0; x < dim.x; ++x)
			{
				sProbe p;
				p.local.set(x, y, z);

				//index in the linear array
				p.index = x + y * dim.x + z * dim.x * dim.y;

				//and its position
				p.pos = start_pos + delta * Vector3(x, y, z);
				probes.push_back(p);
			}

	computeProbeCoefficients(scene);	
	uploadProbes();
}

void Renderer::computeProbeCoefficients(Scene* scene) 
{
	FloatImage images[6]; //here we will store the six views

	//set the fov to 90 and the aspect to 1
	Camera cam;
	cam.setPerspective(90, 1, 0.1, 1000);

	int num = probes.size();
	//now compute the coeffs for every probe
	for (int iP = 0; iP < num; ++iP)
	{
		int probe_index = iP;


		sProbe* p = &probes[iP];

		for (int i = 0; i < 6; ++i) //for every cubemap face
		{
			//compute camera orientation using defined vectors
			Vector3 eye = p->pos;
			Vector3 front = cubemapFaceNormals[i][2];
			Vector3 center = p->pos + front;
			Vector3 up = cubemapFaceNormals[i][1];
			cam.lookAt(eye, center, up);
			cam.enable();

			//render the scene from this point of view
			irr_fbo.bind();
			renderSceneForward(scene, &cam);
			irr_fbo.unbind();

			//read the pixels back and store in a FloatImage
			images[i].fromTexture(irr_fbo.color_textures[0]);
		}

		//compute the coefficients given the six images
		p->sh = computeSH(images);
	}
}

void Renderer::uploadProbes() {
	if (!probes_texture) {
		std::cout << "this should be done only once" << std::endl;
		probes_texture = new Texture(9, probes.size(), GL_RGB, GL_FLOAT);
	}

	SphericalHarmonics* sh_data = NULL;
	sh_data = new SphericalHarmonics[dim.x * dim.y * dim.z];
	
	// fill data
	sProbe p;
	int index;
	for (int z = 0; z < dim.z; ++z)
		for (int y = 0; y < dim.y; ++y)
			for (int x = 0; x < dim.x; ++x)
			{
				index = x + y * dim.x + z * dim.x * dim.y;
				p = probes[index];
				sh_data[index] = p.sh;
			}

	//now upload the data to the GPU
	probes_texture->upload(GL_RGB, GL_FLOAT, false, (uint8*)sh_data);

	//disable any texture filtering when reading
	probes_texture->bind();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	//always free memory after allocating it!!!
	delete[] sh_data;
}

void GTR::Renderer::addRenderCall(RenderCall renderCall)
{
	renderCalls.push_back(renderCall);
}

RenderCall GTR::Renderer::createRenderCall(Matrix44 model, Mesh* mesh, Material* material, float distance_to_camera)
{
	RenderCall renderCall = GTR::RenderCall();
	renderCall.material = material;
	renderCall.mesh = mesh;
	renderCall.model = model;
	renderCall.distance_to_camera = distance_to_camera;
	return renderCall;
}

void GTR::Renderer::collectRCsandLights(GTR::Scene* scene, Camera* camera)
{
	renderCalls.clear();
	scene->l_entities.clear();

	//collect entities
	for (int i = 0; i < scene->entities.size(); ++i)
	{
		BaseEntity* ent = scene->entities[i];
		if (!ent->visible)
			continue;

		//is a prefab!
		if (ent->entity_type == PREFAB)
		{
			PrefabEntity* pent = (GTR::PrefabEntity*)ent;
			if (pent->prefab)
				renderPrefab(ent->model, pent->prefab, camera);
		}

		//is a light
		if (ent->entity_type == LIGHT)
		{
			float aspect = Application::instance->window_width / (float)Application::instance->window_height;
			LightEntity* lent = (GTR::LightEntity*)ent;
			//BoundingBox light_bbox = BoundingBox(lent->model.getTranslation(), Vector3(lent->max_distance, lent->max_distance, lent->max_distance));
			//BoundingBox world_bounding = transformBoundingBox(lent->model, light_bbox);

			//if bounding box is inside the camera frustum then the object is probably visible
			//if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize)) {
			scene->l_entities.push_back(lent);
			if (lent->light_type == SPOT) // SPOT LIGHT CAMERA
			{
				Vector3 eye = lent->model.getTranslation(); // camera position
				Vector3 center = lent->model.rotateVector(Vector3(0, 0, 1));
				lent->light_camera->lookAt(eye, eye + center, Vector3(0, 1, 0));
				lent->light_camera->setPerspective(lent->cone_angle, aspect, 1.0f, lent->max_distance);
			}
			else if (lent->light_type == DIRECTIONAL) // DIRECTIONAL LIGHT CAMERA
			{
				Camera* light_camera = lent->light_camera;
				Vector3 eye = lent->model.getTranslation(); // camera position
				Vector3 center = lent->model.rotateVector(Vector3(0, 0, 1));

				light_camera->lookAt(eye, eye + center, Vector3(0, 1, 0));
				float a_size = lent->area_size;
				light_camera->setOrthographic(-a_size, a_size, -a_size / aspect, a_size / aspect, 10, 10000);
			}
		}
	}

	//std::sort(std::begin(renderCalls), std::end(renderCalls), less_than_alpha());
	//std::sort(std::begin(renderCalls), std::end(renderCalls), less_than_depth());
	std::sort(std::begin(renderCalls), std::end(renderCalls), sort_alpha_depth());

}

void Renderer::renderToFBOForward(GTR::Scene* scene, Camera* camera)
{
	float w = Application::instance->window_width;
	float h = Application::instance->window_height;

	// create lights' FBO
	generateShadowmaps(scene);

	// show scene
	glEnable(GL_DEPTH_TEST);
	glViewport(0, 0, w, h);
	renderScene(scene, camera);
}

void Renderer::renderToFBODeferred(GTR::Scene* scene, Camera* camera) {

	generateShadowmaps(scene);

	gbuffers_fbo.bind();
	gbuffers_fbo.enableSingleBuffer(0);
	
	//clear GB0 with the color (and depth)
	glClearColor(0.1, 0.1, 0.1, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	//and now enable the second GB to clear it to black
	gbuffers_fbo.enableSingleBuffer(1);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	//enable all buffers back
	gbuffers_fbo.enableAllBuffers();

	renderScene(scene, camera);

	gbuffers_fbo.unbind();

	Shader* shader = Shader::Get("depth");
	shader->enable();
	shader->setUniform("u_camera_nearfar", Vector2(camera->near_plane, camera->far_plane));

	float w = Application::instance->window_width;
	float h = Application::instance->window_height;


	if (render_mode == SHOW_GBUFFERS) {
		glDisable(GL_BLEND);
		glViewport(0.0f, 0.0f, w / 2, h / 2);
		gbuffers_fbo.color_textures[0]->toViewport();
		glViewport(w / 2, 0.0f, w / 2, h / 2);
		gbuffers_fbo.color_textures[1]->toViewport();
		glViewport(0.0f, h / 2, w / 2, h / 2);
		gbuffers_fbo.color_textures[2]->toViewport();
		glViewport(w / 2, h / 2, w / 2, h / 2);
		gbuffers_fbo.depth_texture->toViewport(shader);
	}
	else if (render_mode == SHOW_SSAO) {
		generateSSAO(scene, camera);
		glViewport(0.0f, 0.0f, w, h);
		ssao_blur.color_textures[0]->toViewport();
	}
	else { // show deferred all together
		generateSSAO(scene, camera);
		//start rendering to the illumination fbo
		illumination_fbo.bind();

		//create and FBO
		glClearColor(0, 0, 0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		//joinGbuffers(scene, camera);
		illuminationDeferred(scene, camera);
		
		if (show_probe) {
			sProbe probe;
			for (int i = 0; i < probes.size(); i++)
			{
				probe = probes[i];
				renderProbe(probe.pos, 5.0, probe.sh.coeffs[0].v);
			}
		}
		
		illumination_fbo.unbind();
		//be sure blending is not active
		glDisable(GL_BLEND);
		Shader* s_final = NULL;
		if (hdr) s_final = Shader::Get("final");
		glViewport(0.0f, 0.0f, w, h);

		illumination_fbo.color_textures[0]->toViewport(s_final);
	}
	shader->disable();
	
	glDisable(GL_BLEND);

}

void Renderer::illuminationDeferred(GTR::Scene* scene, Camera* camera) {

	float w = Application::instance->window_width;
	float h = Application::instance->window_height;
	Matrix44 inv_vp = camera->viewprojection_matrix;
	inv_vp.inverse();
	//we need a fullscreen quad
	Mesh* quad = Mesh::getQuad();
	// AMBIENT
	Shader* s = Shader::Get("deferred");
	s->enable();

	s->setUniform("u_color_texture", gbuffers_fbo.color_textures[0], 0);
	s->setUniform("u_normal_texture", gbuffers_fbo.color_textures[1], 1);
	s->setUniform("u_extra_texture", gbuffers_fbo.color_textures[2], 2);
	s->setUniform("u_depth_texture", gbuffers_fbo.depth_texture, 3);
	s->setUniform("u_ao_texture", ssao_blur.color_textures[0], 4);
	s->setUniform("u_probes_texture", probes_texture, 6);
	
	//pass the inverse projection of the camera to reconstruct world pos.
	s->setUniform("u_inverse_viewprojection", inv_vp);
	//pass the inverse window resolution, this may be useful
	s->setUniform("u_iRes", Vector2(1.0 / (float)w, 1.0 / (float)h));

	s->setUniform("u_irr_end", end_pos);
	s->setUniform("u_irr_start", start_pos);
	s->setUniform("u_irr_normal_distance", (float)10.0);
	s->setUniform("u_irr_delta", delta);
	s->setUniform("u_irr_dims", dim);
	s->setUniform("u_num_probes", (int)probes.size());

	s->setUniform("u_ambient_light", scene->ambient_light);
	s->setUniform("u_viewprojection", camera->viewprojection_matrix);

	s->setUniform("u_hdr", hdr);

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	quad->render(GL_TRIANGLES);
	s->disable();
	
	Mesh* sphere = Mesh::Get("data/meshes/sphere.obj", true);

	Shader* sh = Shader::Get("deferred_ws");

	sh->enable();
	//pass the gbuffers to the shader
	sh->setUniform("u_color_texture", gbuffers_fbo.color_textures[0], 0);
	sh->setUniform("u_normal_texture", gbuffers_fbo.color_textures[1], 1);
	sh->setUniform("u_extra_texture", gbuffers_fbo.color_textures[2], 2);
	sh->setUniform("u_depth_texture", gbuffers_fbo.depth_texture, 3);
	sh->setUniform("u_ao_texture", ssao_blur.color_textures[0], 4);
	sh->setUniform("u_probes_texture", Texture::getBlackTexture(), 6);

	//pass the inverse projection of the camera to reconstruct world pos.
	sh->setUniform("u_inverse_viewprojection", inv_vp);
	//pass the inverse window resolution, this may be useful
	sh->setUniform("u_iRes", Vector2(1.0 / (float)w, 1.0 / (float)h));
	
	//scene->ambient_light
	sh->setUniform("u_ambient_light", Vector3(0, 0, 0));
	sh->setUniform("u_viewprojection", camera->viewprojection_matrix);
	sh->setUniform("u_camera_eye", camera->eye);
	sh->setUniform("u_hdr", hdr);

	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CW);

	glBlendFunc(GL_ONE, GL_ONE);

	std::vector<LightEntity*> directionals;

	
	for (int i = 0; i < scene->l_entities.size(); ++i) {
		LightEntity* lent = scene->l_entities[i];
		if (!lent->visible) continue;
		lent->setUniforms(sh);

		if (lent->light_type == POINT || lent->light_type == SPOT)
		{
			Matrix44 m;
			m.setTranslation(lent->model.getTranslation().x, lent->model.getTranslation().y, lent->model.getTranslation().z);
			m.scale(lent->max_distance, lent->max_distance, lent->max_distance); //and scale it according to the max_distance of the light
			sh->setUniform("u_model", m); //pass the model to the shader to render the sphere

			sphere->render(GL_TRIANGLES);
		}
		else if (lent->light_type == DIRECTIONAL) {
			directionals.push_back(lent);
		}
	}
	
	// DIRECTIONAL
	glDisable(GL_CULL_FACE);
	glFrontFace(GL_CCW);
	glDisable(GL_DEPTH_TEST);
	s->enable();

	for (int i = 0; i < directionals.size(); ++i)
	{
		LightEntity* lent = directionals[i];
		if (!lent->visible) continue;

		lent->setUniforms(s);

		s->setUniform("u_color_texture", gbuffers_fbo.color_textures[0], 0);
		s->setUniform("u_normal_texture", gbuffers_fbo.color_textures[1], 1);
		s->setUniform("u_extra_texture", gbuffers_fbo.color_textures[2], 2);
		s->setUniform("u_depth_texture", gbuffers_fbo.depth_texture, 3);
		s->setUniform("u_ao_texture", ssao_blur.color_textures[0], 4);
		s->setUniform("u_probes_texture", Texture::getBlackTexture(), 6);
			
		//pass the inverse projection of the camera to reconstruct world pos.
		s->setUniform("u_inverse_viewprojection", inv_vp);
		//pass the inverse window resolution, this may be useful
		s->setUniform("u_iRes", Vector2(1.0 / (float)w, 1.0 / (float)h));
			
		s->setUniform("u_ambient_light", Vector3(0, 0, 0));
		s->setUniform("u_viewprojection", camera->viewprojection_matrix);
		s->setUniform("u_hdr", hdr);

		quad->render(GL_TRIANGLES);
	}
	s->disable();
	directionals.clear();
	
	glFrontFace(GL_CCW);
}

void Renderer::generateSSAO(GTR::Scene* scene, Camera* camera)
{
	gbuffers_fbo.depth_texture->bind();
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	//start rendering inside the ssao texture
	ssao_fbo.bind();

	Mesh* quad = Mesh::getQuad();

	Matrix44 invvp = camera->viewprojection_matrix;
	invvp.inverse();

	//get the shader for SSAO (remember to create it using the atlas)
	Shader* shader = Shader::Get("ssao");
	shader->enable();

	//send info to reconstruct the world position
	shader->setUniform("u_inverse_viewprojection", invvp);
	shader->setTexture("u_depth_texture", gbuffers_fbo.depth_texture, 0);
	shader->setTexture("u_normal_texture", gbuffers_fbo.color_textures[1], 1);
	//we need the pixel size so we can center the samples 
	shader->setUniform("u_iRes", Vector2(1.0 / (float)gbuffers_fbo.depth_texture->width,
		1.0 / (float)gbuffers_fbo.depth_texture->height));
	//we will need the viewprojection to obtain the uv in the depthtexture of any random position of our world
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	

	//send random points so we can fetch around
	shader->setUniform3Array("u_points", (float*)&random_points[0],
		random_points.size());

	glDisable(GL_DEPTH_TEST);
	//render fullscreen quad
	quad->render(GL_TRIANGLES);

	glEnable(GL_DEPTH_TEST);
	//stop rendering to the texture
	ssao_fbo.unbind();

	ssao_fbo.color_textures[0]->copyTo(ssao_blur.color_textures[0]);

	// BLUR
	if (blur_ssao) {
		ssao_blur.bind();
		
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		Mesh* quad = Mesh::getQuad();
		shader = Shader::Get("blur");
		shader->enable();
		shader->setTexture("u_ssao_texture", ssao_blur.color_textures[0], 0);
		shader->setUniform("horizontal", true);
		quad->render(GL_TRIANGLES);
		shader->setUniform("horizontal", false);
		quad->render(GL_TRIANGLES);
		shader->disable();

		ssao_blur.unbind();
	}

}

std::vector<Vector3> Renderer::generateSpherePoints(int num, float radius, bool hemi)
{
	std::vector<Vector3> points;
	points.resize(num);
	for (int i = 0; i < num; i += 3)
	{
		Vector3& p = points[i];
		float u = random();
		float v = random();
		float theta = u * 2.0 * PI;
		float phi = acos(2.0 * v - 1.0);
		float r = cbrt(random() * 0.9 + 0.1) * radius;
		float sinTheta = sin(theta);
		float cosTheta = cos(theta);
		float sinPhi = sin(phi);
		float cosPhi = cos(phi);
		p.x = r * sinPhi * cosTheta;
		p.y = r * sinPhi * sinTheta;
		p.z = r * cosPhi;
		if (hemi && p.z < 0)
			p.z *= -1.0;
	}
	return points;
}

void Renderer::renderToFBO(GTR::Scene* scene, Camera* camera) {

	switch (pipeline_mode) {
	case FORWARD: renderToFBOForward(scene, camera); break;
	case DEFERRED: renderToFBODeferred(scene, camera); break;
	}
}

void Renderer::renderMeshDeferred(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera) {

	Shader* shader = Shader::Get("multi");
	Texture* texture = NULL;
	Texture* normal_texture = NULL;
	Texture* mat_properties_texture = NULL;

	texture = material->color_texture.texture;
	if (texture == NULL) texture = Texture::getWhiteTexture(); //a 1x1 white texture

	bool read_normal = true;
	normal_texture = material->normal_texture.texture;
	if (!normal_texture) read_normal = false;

	mat_properties_texture = material->metallic_roughness_texture.texture;
	if (mat_properties_texture == NULL) mat_properties_texture = Texture::getWhiteTexture(); //a 1x1 white texture
	
	bool changed = false;
	if (!dithering && material->alpha_mode == GTR::eAlphaMode::BLEND) return;
	else if (dithering && material->alpha_mode != GTR::eAlphaMode::BLEND) { dithering = false; changed = true; }
	else glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if (material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	shader->enable();

	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model);
	float t = getTime();
	shader->setUniform("u_time", t);

	shader->setUniform("u_color", material->color);
	shader->setUniform("u_emissive_factor", material->emissive_factor);
	if (texture) shader->setUniform("u_texture", texture, 0);
	if (normal_texture) shader->setUniform("u_normal_texture", normal_texture, 1);
	if (mat_properties_texture) shader->setUniform("u_mat_properties_texture", mat_properties_texture, 2);
	shader->setUniform("u_read_normal", read_normal);
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);
	shader->setUniform("u_dither", dithering);

	mesh->render(GL_TRIANGLES);
	shader->disable();
	glDisable(GL_BLEND);

	if (changed) dithering = true;
}

void Renderer::renderScene(GTR::Scene* scene, Camera* camera)
{
	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	collectRCsandLights(scene, camera);

	for (int i = 0; i < renderCalls.size(); ++i)
	{
		RenderCall render_call = renderCalls[i];
		if (pipeline_mode == FORWARD)
			renderMeshWithMaterial(render_call.model, render_call.mesh, render_call.material, camera);
		else {
			if (dithering) renderMeshDeferred(render_call.model, render_call.mesh, render_call.material, camera);
			else {
				if (render_call.material->alpha_mode == BLEND)
					renderMeshWithMaterial(render_call.model, render_call.mesh, render_call.material, camera);
				else renderMeshDeferred(render_call.model, render_call.mesh, render_call.material, camera);
			}

		}

	}
}

void GTR::Renderer::renderSceneForward(GTR::Scene* scene, Camera* camera) {
	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	collectRCsandLights(scene, camera);

	for (int i = 0; i < renderCalls.size(); ++i)
	{
		RenderCall render_call = renderCalls[i];
		renderMeshWithMaterial(render_call.model, render_call.mesh, render_call.material, camera, scene);
	}
}

void GTR::Renderer::renderShadow(GTR::Scene* scene, Camera* camera)
{
	//set the clear color (the background color)
	glClearColor(0, 0, 0, 0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	collectRCsandLights(scene, camera);

	for (int i = 0; i < renderCalls.size(); ++i)
	{
		RenderCall render_call = renderCalls[i];
		getShadows(render_call.model, render_call.mesh, render_call.material, camera);
	}
}

void GTR::Renderer::generateShadowmaps(GTR::Scene* scene)
{
	for (int i = 0; i < scene->l_entities.size(); ++i) {
		LightEntity* light = scene->l_entities[i];

		if (light->light_type != POINT) {
			if (light->fbo.fbo_id == 0)
			{
				light->fbo = FBO();
				light->fbo.setDepthOnly(2048, 2048);
				light->shadow_buffer = new Texture();
			}

			light->fbo.bind();
			glColorMask(false, false, false, false);
			glClear(GL_DEPTH_BUFFER_BIT);

			renderShadow(scene, light->light_camera);

			light->fbo.unbind();

			glColorMask(true, true, true, true);

			light->shadow_buffer = light->fbo.depth_texture;
		}
	}
}

//renders all the prefab
void Renderer::renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera)
{
	assert(prefab && "PREFAB IS NULL");
	//assign the model to the root node
	renderNode(model, &prefab->root, camera);
}

//renders a node of the prefab and its children
void Renderer::renderNode(const Matrix44& prefab_model, GTR::Node* node, Camera* camera)
{
	GTR::Scene* scene = GTR::Scene::instance;

	if (!node->visible)
		return;

	//compute global matrix
	Matrix44 node_model = node->getGlobalMatrix(true) * prefab_model;

	//does this node have a mesh? then we must render it
	if (node->mesh && node->material)
	{
		//compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
		BoundingBox world_bounding = transformBoundingBox(node_model, node->mesh->box);

		//if bounding box is inside the camera frustum then the object is probably visible
		if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize))
		{
			//render node mesh
			//renderMeshWithMaterial( node_model, node->mesh, node->material, camera );
			float distance_to_camera = world_bounding.center.distance(camera->eye);
			RenderCall render_Call = createRenderCall(node_model, node->mesh, node->material, distance_to_camera);
			addRenderCall(render_Call);

			//node->mesh->renderBounding(node_model, true);
		}
	}

	//iterate recursively with children
	for (int i = 0; i < node->children.size(); ++i)
		renderNode(prefab_model, node->children[i], camera);
}

//renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera, Scene* scene)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	Shader* shader = NULL;
	Texture* texture = NULL;
	Texture* mr_texture = NULL;
	Texture* emissive_texture = NULL;
	Texture* occ_texture = NULL;
	Texture* normal_texture = NULL;
	bool have_normalmap = true;

	if(scene == nullptr) scene = GTR::Scene::instance;
	//GTR::Scene* scene = GTR::Scene::instance;

	// textures
	texture = material->color_texture.texture;
	emissive_texture = material->emissive_texture.texture;
	mr_texture = material->metallic_roughness_texture.texture;
	normal_texture = material->normal_texture.texture;
	occ_texture = material->occlusion_texture.texture;

	if (texture == NULL) texture = Texture::getWhiteTexture(); //a 1x1 white texture
	if (mr_texture == NULL) mr_texture = Texture::getWhiteTexture(); //a 1x1 white texture
	if (emissive_texture == NULL) emissive_texture = Texture::getWhiteTexture(); //a 1x1 white texture
	if (occ_texture == NULL) occ_texture = Texture::getWhiteTexture(); //a 1x1 white texture
	//if (normal_texture == NULL) normal_texture = Texture::getWhiteTexture(); //a 1x1 white texture
	if (normal_texture == NULL) have_normalmap = false;

	//select the blending
	if (material->alpha_mode == GTR::eAlphaMode::BLEND) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if (material->two_sided) glDisable(GL_CULL_FACE);
	else glEnable(GL_CULL_FACE);

	assert(glGetError() == GL_NO_ERROR);

	//chose a shader
	switch (render_mode) {
		case SHOW_NORMAL: shader = Shader::Get("normal"); break;
		case SHOW_UVS: shader = Shader::Get("uvs"); break;
		case SHOW_TEXTURE: shader = Shader::Get("texture"); break;
		case SHOW_DEFERRED: shader = Shader::Get("texture"); break;
		case SHOW_AO: shader = Shader::Get("occlusion"); break;
		case DEFAULT: shader = Shader::Get("light_singlepass"); break;
		case SHOW_MULTI: shader = Shader::Get("light_multipass"); break;
	}

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model);
	float t = getTime();
	shader->setUniform("u_time", t);

	shader->setUniform("u_color", material->color);
	if (texture) shader->setUniform("u_texture", texture, 0);
	if (mr_texture) shader->setUniform("u_metallic_roughness_texture", mr_texture, 1);
	if (emissive_texture) shader->setUniform("u_emissive_texture", emissive_texture, 2);
	if (occ_texture) shader->setUniform("u_occ_texture", occ_texture, 3);
	if (normal_texture) shader->setUniform("u_normal_texture", normal_texture, 4);
	shader->setUniform("u_read_normal", have_normalmap);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);

	// light information
	shader->setUniform("u_ambient_light", scene->ambient_light);
	shader->setUniform("u_emissive_factor", material->emissive_factor);


	// SINGLE PASS
	if (render_mode == DEFAULT)
	{
		// lights
		int num_lights = scene->l_entities.size();
		Vector3 light_position[5];
		Vector3 light_color[5];
		Vector3 light_vector[5];
		int light_type[5] = {};
		float max_distances[5];
		float light_intensities[5];
		float light_cos_cutoff[5];
		float light_exponents[5];

		for (int j = 0; j < num_lights; ++j)
		{
			LightEntity* lent = scene->l_entities[j];
			light_color[j] = lent->color;
			light_position[j] = lent->model.getTranslation(); //convert a position from local to world
			light_type[j] = static_cast<int>(lent->light_type);
			if (lent->light_type == SPOT)
				light_vector[j] = lent->model.frontVector();
			else if (lent->light_type == DIRECTIONAL)
				light_vector[j] = lent->model.rotateVector(Vector3(0, 0, -1));
			if (lent->light_type == SPOT) {
				light_cos_cutoff[j] = cos(lent->cone_angle * DEG2RAD);
				light_exponents[j] = lent->exponent;
			}
			max_distances[j] = lent->max_distance;
			light_intensities[j] = lent->intensity;
		}

		shader->setUniform3Array("u_light_pos", (float*)&light_position, 5);
		shader->setUniform3Array("u_light_color", (float*)&light_color, 5);
		shader->setUniform1Array("u_light_type", (int*)&light_type, 5);
		shader->setUniform3Array("u_direction", (float*)&light_vector, 5);
		shader->setUniform1("u_num_lights", num_lights);
		shader->setUniform1Array("u_maxdist", (float*)&max_distances, 5);
		shader->setUniform1Array("u_light_factor", (float*)&light_intensities, 5);
		shader->setUniform1Array("u_spotCosineCutoff", (float*)&light_cos_cutoff, 5);
		shader->setUniform1Array("u_spotExponent", (float*)&light_exponents, 5);

		//do the draw call that renders the mesh into the screen
		mesh->render(GL_TRIANGLES);
	}

	// MULTI PASS
	else if (render_mode == SHOW_MULTI)
	{
		//allow to render pixels that have the same depth as the one in the depth buffer
		glDepthFunc(GL_LEQUAL);

		//set blending mode to additive, this will collide with materials with blend...
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);

		for (int i = 0; i < scene->l_entities.size(); ++i)
		{
			LightEntity* light = scene->l_entities[i];
			//first pass doesn't use blending
			if (i == 0) {
				if (material->alpha_mode == GTR::eAlphaMode::BLEND) {
					glEnable(GL_BLEND);
					glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				}
				else {
					glBlendFunc(GL_SRC_ALPHA, GL_ONE);
					glDisable(GL_BLEND);
				}
			}
			else {
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE);
				shader->setUniform("u_ambient_light", Vector3(0, 0, 0));
				shader->setUniform("u_emissive_factor", Vector3(0, 0, 0));
			}

			light->setUniforms(shader);

			//render the mesh
			mesh->render(GL_TRIANGLES);
		}

		glDepthFunc(GL_LESS); //as default
	}

	else
		mesh->render(GL_TRIANGLES); //do the draw call that renders the mesh into the screen

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glDepthFunc(GL_LESS);
}

void GTR::Renderer::resize(int width, int height)
{
	illumination_fbo.create(width, height, 3, GL_RGB, GL_FLOAT, false);
}

void GTR::Renderer::getShadows(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	Shader* shader = NULL;

	GTR::Scene* scene = GTR::Scene::instance;

	//select the blending
	if (material->alpha_mode == GTR::eAlphaMode::BLEND)
		return;
	else
		glDisable(GL_BLEND);

	if (material->two_sided) glDisable(GL_CULL_FACE);
	else glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	shader = Shader::Get("shadow");

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model);
	shader->setUniform("u_color", material->color);
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);

	Texture* texture = NULL;
	texture = material->color_texture.texture;
	if (texture == NULL) texture = Texture::getWhiteTexture(); //a 1x1 white texture
	if (texture) shader->setUniform("u_texture", texture, 0);


	glDepthFunc(GL_LESS); //as default

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
}

void GTR::Renderer::joinGbuffers(GTR::Scene* scene, Camera* camera)
{
	float w = Application::instance->window_width;
	float h = Application::instance->window_height;
	Matrix44 inv_vp = camera->viewprojection_matrix;
	inv_vp.inverse();
	//we need a fullscreen quad
	Mesh* quad = Mesh::getQuad();
	//Mesh* sphere = Mesh::Get("data/meshes/sphere.obj", true);

	//we need a shader specially for this task, lets call it "deferred"
	Shader* sh = Shader::Get("deferred");
	//Shader* sh = Shader::Get("deferred_ws");
	sh->enable();

	//pass the gbuffers to the shader
	sh->setUniform("u_color_texture", gbuffers_fbo.color_textures[0], 0);
	sh->setUniform("u_normal_texture", gbuffers_fbo.color_textures[1], 1);
	sh->setUniform("u_extra_texture", gbuffers_fbo.color_textures[2], 2);
	sh->setUniform("u_depth_texture", gbuffers_fbo.depth_texture, 3);

	//pass the inverse projection of the camera to reconstruct world pos.
	sh->setUniform("u_inverse_viewprojection", inv_vp);
	//pass the inverse window resolution, this may be useful
	sh->setUniform("u_iRes", Vector2(1.0 / (float)w, 1.0 / (float)h));

	//pass all the information about the light and ambient…
	sh->setUniform("u_ambient_light", scene->ambient_light);

	quad->render(GL_TRIANGLES);

	glDisable(GL_DEPTH_TEST);
	sh->disable();
	//glFrontFace(GL_CCW);
}

Texture* GTR::CubemapFromHDRE(const char* filename)
{
	HDRE* hdre = HDRE::Get(filename);
	if (!hdre)
		return NULL;

	Texture* texture = new Texture();
	if (hdre->getFacesf(0))
	{
		texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFacesf(0),
			hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_FLOAT);
		for (int i = 1; i < hdre->levels; ++i)
			texture->uploadCubemap(texture->format, texture->type, false,
				(Uint8**)hdre->getFacesf(i), GL_RGBA32F, i);
	}
	else
		if (hdre->getFacesh(0))
		{
			texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFacesh(0),
				hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_HALF_FLOAT);
			for (int i = 1; i < hdre->levels; ++i)
				texture->uploadCubemap(texture->format, texture->type, false,
					(Uint8**)hdre->getFacesh(i), GL_RGBA16F, i);
		}
	return texture;
}
