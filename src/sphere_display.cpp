/* Copyright (c) 2017 Nuclear and Applied Robotics Group, University of Texas at Austin.
 * SphereDisplay class implementation.
 *
 * Author: Veiko Vunder 
 *
 * Based on the rviz_textured_quads package by Felipe Bacim..
 *
 */

#include <OGRE/OgreArchive.h>
#include <OGRE/OgreFrustum.h>
#include <OGRE/OgreMaterialManager.h>
#include <OGRE/OgreMeshManager.h>
#include <OGRE/OgreMovableObject.h>
#include <OGRE/OgreSceneManager.h>
#include <OGRE/OgreSceneNode.h>
#include <OGRE/OgreHardwareVertexBuffer.h>
#include <OGRE/OgreHardwareBufferManager.h>
#include <OGRE/OgreMesh.h>
#include <OGRE/OgreSubMesh.h>
#include <OGRE/OgreAxisAlignedBox.h>
//#include <OGRE/OgreHardwarePixelBuffer.h>
#include <cv_bridge/cv_bridge.h>
#include <image_transport/camera_common.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <rviz/display_context.h>
#include <rviz/ogre_helpers/shape.h>
#include <rviz/properties/float_property.h>
#include <rviz/properties/ros_topic_property.h>
#include <rviz/properties/tf_frame_property.h>
#include <rviz/render_panel.h>
#include <rviz/robot/robot.h>
#include <rviz/robot/tf_link_updater.h>
#include <rviz/validate_floats.h>
#include <rviz/view_manager.h>
#include <rviz/visualization_manager.h>
#include <rviz/image/ros_image_texture.h>
#include <rviz_textured_sphere/sphere_display.h>
#include <angles/angles.h>
#include <sensor_msgs/image_encodings.h>
#include <string>
#include <vector>

namespace rviz
{

bool validateFloats(const sensor_msgs::CameraInfo& msg)
{
  bool valid = true;
  valid = valid && validateFloats(msg.D);
  valid = valid && validateFloats(msg.K);
  valid = valid && validateFloats(msg.R);
  valid = valid && validateFloats(msg.P);
  return valid;
}

SphereDisplay::SphereDisplay() :
	Display(),
	texture_front_(NULL),
	texture_rear_(NULL),
	sphere_node_(NULL),
	new_front_image_arrived_(false),
	new_rear_image_arrived_(false)
{
  image_topic_front_property_ = new RosTopicProperty("Front camera image", "",
      QString::fromStdString(ros::message_traits::datatype<sensor_msgs::Image>()),
      "Image topic of the front camera to subscribe to.",
      this, SLOT(onImageTopicChanged()));
  image_topic_rear_property_ = new RosTopicProperty("Rear camera image", "",
      QString::fromStdString(ros::message_traits::datatype<sensor_msgs::Image>()),
      "Image topic of the rear camera to subscribe to.",
      this, SLOT(onImageTopicChanged()));
  tf_frame_property_ = new TfFrameProperty("Reference frame", "<Fixed Frame>",
      "Position the sphere relative to this frame.",
      this, 0, true);

  fov_front_property_ = new FloatProperty("FOV front", 235.0,
      "Front camera field of view", this, SLOT(onMeshParamChanged()));

  fov_rear_property_= new FloatProperty("FOV rear", 235.0,
      "Rear camera field of view", this, SLOT(onMeshParamChanged()));

  debug_property_= new FloatProperty("Debug value", 0.0f,
	  "A value for debugging", this, SLOT(onDebugValueChanged()));

  // Create and load a separate resourcegroup
  std::string path_str = ros::package::getPath(ROS_PACKAGE_NAME);
  Ogre::ResourceGroupManager::getSingleton().addResourceLocation( path_str + "/ogre_media", "FileSystem", ROS_PACKAGE_NAME  );
  Ogre::ResourceGroupManager::getSingleton().initialiseResourceGroup(ROS_PACKAGE_NAME);
}

SphereDisplay::~SphereDisplay()
{
  unsubscribe();
  delete texture_front_;
  delete texture_rear_;
  delete image_topic_front_property_;
  delete image_topic_rear_property_;
  delete tf_frame_property_;
  delete fov_front_property_;
  delete fov_rear_property_;
  delete debug_property_;
}

void SphereDisplay::onInitialize()
{
	tf_frame_property_->setFrameManager(context_->getFrameManager());
	createSphere();
	Display::onInitialize();
}


void SphereDisplay::createSphere()
{
	// Return if node already exists.
	Ogre::String node_name(ROS_PACKAGE_NAME "_node");
	Ogre::String material_name(ROS_PACKAGE_NAME "_material");

	if(scene_manager_->hasSceneNode(node_name))
	{
		return; 
	}


	sphere_material_ = Ogre::MaterialManager::getSingleton().getByName(material_name, ROS_PACKAGE_NAME);
	if(sphere_material_.isNull())
	{
		ROS_ERROR("createSphere(): couldn't get material '%s'", material_name.c_str());
		return;
	}
	sphere_material_->setReceiveShadows(false);
	sphere_material_->getTechnique(0)->setLightingEnabled(false);
//	Ogre::Pass* pass = sphere_material_->getTechnique(0)->getPass(0);
	
	//pass->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
	//pass->setSceneBlending(Ogre::SBT_REPLACE);
	//pass->setSceneBlendOperation
//	sphere_material_->getTechnique(0)->getPass(0)->setCullingMode(Ogre::CULL_NONE);
	//		material->getTechnique(0)->getPass(0)->setPolygonMode(Ogre::PM_WIREFRAME);

	// Create sphere node and and add mesh entity to the scene
	sphere_node_ = scene_manager_->getRootSceneNode()->createChildSceneNode(
			node_name, Ogre::Vector3( 0, 0, 0  ));

	    Ogre::Quaternion r1, r2; // Rotate from RViz coordinates to OpenGL coordinates
	    r1.FromAngleAxis(Ogre::Radian(M_PI*0.5), Ogre::Vector3::UNIT_X);
	    r2.FromAngleAxis(Ogre::Radian(-M_PI*0.5), Ogre::Vector3::UNIT_Y);
	sphere_node_->rotate(r1*r2);

	sphere_node_->setDirection(Ogre::Vector3(1,0,0));
//	sphere_node_->setScale(1,1,1);
	//Ogre::Entity* sphere_entity = scene_manager_->createEntity(entity_name, Ogre::SceneManager::PT_SPHERE);
	Ogre::MeshPtr sphere_mesh = createSphereMesh(ROS_PACKAGE_NAME "_mesh", 10, 64, 64);
	Ogre::Entity* sphere_entity = scene_manager_->createEntity(sphere_mesh);
	sphere_entity->setMaterialName(material_name);
	sphere_node_->attachObject(sphere_entity);
}


Ogre::MeshPtr SphereDisplay::createSphereMesh(const std::string& mesh_name, const double r, const unsigned int ring_cnt = 32, const unsigned int segment_cnt = 32)
{
	Ogre::MeshPtr mesh = Ogre::MeshManager::getSingleton().createManual(mesh_name, ROS_PACKAGE_NAME);
	Ogre::SubMesh* sub_mesh = mesh -> createSubMesh();
	mesh->sharedVertexData = new Ogre::VertexData();
	Ogre::VertexData* vertex_data = mesh->sharedVertexData;

	// Define vertex format
	// Position, Normal, and UV coord0, UV coord1
	Ogre::VertexDeclaration* vertex_decl = vertex_data -> vertexDeclaration;
	size_t cur_offset = 0;
	vertex_decl -> addElement(0, cur_offset, Ogre::VET_FLOAT3, Ogre::VES_POSITION);
	cur_offset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3);
	vertex_decl -> addElement(0, cur_offset, Ogre::VET_FLOAT3, Ogre::VES_NORMAL);
	cur_offset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3);
	vertex_decl -> addElement(0, cur_offset, Ogre::VET_FLOAT2, Ogre::VES_TEXTURE_COORDINATES, 0);
	cur_offset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT2);
	vertex_decl -> addElement(0, cur_offset, Ogre::VET_FLOAT2, Ogre::VES_TEXTURE_COORDINATES, 1);
	cur_offset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT2);

	// Allocate vertex buffer
	vertex_data -> vertexCount = (ring_cnt + 1) * (segment_cnt + 1);
	Ogre::HardwareVertexBufferSharedPtr v_buf = Ogre::HardwareBufferManager::getSingleton().createVertexBuffer(vertex_decl->getVertexSize(0), vertex_data->vertexCount, Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY, false);
	Ogre::VertexBufferBinding* binding = vertex_data -> vertexBufferBinding;
	binding -> setBinding(0,v_buf);
	float* vertex = static_cast<float*>(v_buf -> lock(Ogre::HardwareBuffer::HBL_DISCARD));
	
	// Allocate index buffer
	sub_mesh -> indexData -> indexCount = 6 * ring_cnt * (segment_cnt + 1);
	sub_mesh -> indexData -> indexBuffer = Ogre::HardwareBufferManager::getSingleton().createIndexBuffer(
			Ogre::HardwareIndexBuffer::IT_16BIT, 
			sub_mesh -> indexData -> indexCount,
			Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY,
			false);
	Ogre::HardwareIndexBufferSharedPtr i_buf = sub_mesh -> indexData ->indexBuffer;
	unsigned short* indices = static_cast<unsigned short*>(i_buf -> lock(Ogre::HardwareBuffer::HBL_DISCARD));

	float delta_ring_angle = M_PI / ring_cnt;
	float delta_segment_angle = 2 * M_PI / segment_cnt;
	unsigned short vertex_index = 0;	

	// For over the rings of the sphere
	for(int ring = 0; ring <= ring_cnt; ring++)
	{
		float r0 = r * sinf(ring * delta_ring_angle);
		float y0 = r * cosf(ring * delta_ring_angle);

		// For over the segments of the sphere
		for(int seg = 0; seg <= segment_cnt; seg++)
		{
			float x0 = r0 * sinf(seg * delta_segment_angle);
			float z0 = r0 * cosf(seg * delta_segment_angle);

			// Add vertex pos
			*vertex++ = x0;
			*vertex++ = y0;
			*vertex++ = z0;

			// Add vertex normal (pointing inwards)
			Ogre::Vector3 normal = -Ogre::Vector3(x0, y0, z0).normalisedCopy();
			*vertex++ = normal.x;
			*vertex++ = normal.y;
			*vertex++ = normal.z;

			// Add uv coordinates
			float lens_fov = angles::from_degrees(180);
			float cropped_fov = angles::from_degrees(180);
			float scaling_factor = M_PI/lens_fov;

			// Coord 0 [-1...1]
			float view_center_front = M_PI;
			float view_center_rear = 2*M_PI;
			float v_angle = ring*delta_ring_angle;
			float v_arg = (v_angle+lens_fov/2-M_PI_2)*scaling_factor;
			float uv_r0 = sinf(v_arg);
			float v_front = cosf(v_arg);
			float v_rear = cosf(v_arg);
			float u_angle = seg*delta_segment_angle;
			float u_arg_front = (u_angle+lens_fov/2 - view_center_front)*scaling_factor;
			float u_arg_rear = (u_angle+lens_fov/2 - view_center_rear)*scaling_factor;
			float u_front = uv_r0 * cosf(u_arg_front);
			float u_rear = uv_r0 * cosf(u_arg_rear);

			//scale and scroll textures so that their centers will align with the centers of half spheres
			u_front = u_front * 0.5 * debug_property_->getFloat() + 0.5;
			v_front = v_front * 0.5  * debug_property_->getFloat()+ 0.5;
			u_rear = u_rear * 0.5 * debug_property_->getFloat() + 0.5;
			v_rear = v_rear * 0.5  * debug_property_->getFloat()+ 0.5;
			
			if(u_angle <= view_center_front-cropped_fov/2 || u_angle >= view_center_front+cropped_fov/2)
			{
				//map out of interest uv coordinates to a circle out of texture boundaries
				u_front = u_front * 0.5+0.5;
				v_front = v_front * 0.5+0.5;
				Ogre::Vector2 uv_front = Ogre::Vector2(u_front,v_front);
				uv_front.normalise();
				uv_front = uv_front*10;
				u_front = uv_front.x;
				v_front = uv_front.y;

				if (u_front==0 && v_front==0)
				{
					u_front=10;
					v_front=10;
				}
			}

			if(u_angle <= view_center_rear-cropped_fov/2 || u_angle >= view_center_rear+cropped_fov/2)
			{
				//map out of interest uv coordinates to a circle out of texture boundaries
				u_rear = u_rear * 0.5+0.5;
				v_rear = v_rear * 0.5+0.5;
				Ogre::Vector2 uv_rear = Ogre::Vector2(u_rear,v_rear);
				uv_rear.normalise();
				uv_rear = uv_rear*10;
				u_rear = uv_rear.x;
				v_rear = uv_rear.y;

				if (u_rear==0 && v_rear==0)
				{
					u_rear=10;
					v_rear=10;
				}
			}

			//Coord 0
			*vertex++ = u_front;
			*vertex++ = 1-v_front;

			//Coord 1
			*vertex++ = u_rear;
			*vertex++ = 1-v_rear;

//				ROS_INFO("Uarg %f, Varg %f", angles::to_degrees(u_arg), angles::to_degrees(v_arg));

			// Add faces (normal inwards)
			if(ring != ring_cnt)	
			{
				*indices++ = vertex_index + segment_cnt + 1;
				*indices++ = vertex_index + segment_cnt;
				*indices++ = vertex_index;               
				*indices++ = vertex_index + 1;
				*indices++ = vertex_index + segment_cnt + 1;
				*indices++ = vertex_index;
				vertex_index++;
			}

		}
	}

	// Unlock buffers
	v_buf -> unlock();
	i_buf -> unlock();

	sub_mesh -> useSharedVertices = true;
	mesh -> _setBounds(Ogre::AxisAlignedBox(Ogre::Vector3(-r, -r, -r), Ogre::Vector3(r, r, r)), false);
	mesh -> _setBoundingSphereRadius(r);
	mesh -> load();

	return mesh;
}


void SphereDisplay::updateFrontCameraImage(const sensor_msgs::Image::ConstPtr& image)
{
	//ROS_WARN("New FRONT image arrived");
	cur_image_front_ = image;
	new_front_image_arrived_ = true;
}

void SphereDisplay::updateRearCameraImage(const sensor_msgs::Image::ConstPtr& image)
{
	//ROS_WARN("New REAR image arrived");
	cur_image_rear_ = image;
	new_rear_image_arrived_ = true;
}


void SphereDisplay::onImageTopicChanged()
{
	//ROS_INFO("onImageTopicChanged()");

	unsubscribe();
	subscribe();
}

void SphereDisplay::onDebugValueChanged()
{
	ROS_WARN("Value changed");
	onMeshParamChanged();
}

void SphereDisplay::onMeshParamChanged()
{
	Ogre::String node_name(ROS_PACKAGE_NAME "_node");
	Ogre::String mesh_name(ROS_PACKAGE_NAME "_mesh");
	scene_manager_->getRootSceneNode()->removeAndDestroyChild(node_name);
	Ogre::MeshManager::getSingleton().remove(mesh_name);
	createSphere();
}


void SphereDisplay::subscribe()
{
	//ROS_INFO("subscribe()");
	if (!isEnabled())
	{
		return;
	}

	if (!image_topic_front_property_->getTopic().isEmpty())
	{
		try
		{
			image_sub_front_ = nh_.subscribe(image_topic_front_property_->getTopicStd(),
					1, &SphereDisplay::updateFrontCameraImage, this);
			setStatus(StatusProperty::Ok, "Front camera image", "OK");
			//ROS_WARN("SUBS FRONT OK");
		}
		catch (ros::Exception& e)
		{
			setStatus(StatusProperty::Error, "Front camera image", QString("Error subscribing: ") + e.what());
			ROS_ERROR("SUBS FRONT FAILED");
		}
	}

	if (!image_topic_rear_property_->getTopic().isEmpty())
	{
		try
		{
			image_sub_rear_ = nh_.subscribe(image_topic_rear_property_->getTopicStd(),
					1, &SphereDisplay::updateRearCameraImage, this);
			//ROS_WARN("SUBS REAR OK");
		} catch (ros::Exception& e)
		{
			setStatus(StatusProperty::Error, "Rear camera image", QString("Error subscribing: ") + e.what());
			ROS_ERROR("SUBS REAR FAILED");
		}
	}
}

void SphereDisplay::unsubscribe()
{
	
	//ROS_INFO("unsubscribe()");
	image_sub_front_.shutdown();
	image_sub_rear_.shutdown();
}


void SphereDisplay::onEnable()
{
	//ROS_INFO("onEnable()");

	subscribe();
}

void SphereDisplay::onDisable()
{
	unsubscribe();
}

void SphereDisplay::preRenderTargetUpdate(const Ogre::RenderTargetEvent& evt)
{
}

void SphereDisplay::update(float wall_dt, float ros_dt)
{
	//ROS_INFO("Update: new front image, %s", new_front_image_arrived_ ? "True":"False");
	//ROS_INFO("Update: new rear image, %s", new_rear_image_arrived_ ? "True":"False");
	// Update front texture
	if(new_front_image_arrived_)
	{
		//ROS_INFO("1update front texture %p", texture_front_);
		imageToTexture(texture_front_, cur_image_front_);
		//ROS_INFO("2update front texture %p", texture_front_);
		updateTexture(texture_front_);
		//ROS_INFO("3update front texture %p", texture_front_);
		new_front_image_arrived_ = false;
	}	

	// Update rear texture
	if(new_rear_image_arrived_)
	{
		imageToTexture(texture_rear_, cur_image_rear_);
		updateTexture(texture_rear_);
		new_rear_image_arrived_ = false;
	}	

	context_->queueRender();

	if(sphere_node_)
	{
		sphere_node_->needUpdate();

//		if(!sphere_material_.isNull())
//		{
//			ROS_INFO("%d", sphere_material_->getTechnique(0)->getPass(0)->getNumTextureUnitStates());
//		}
	}
}


void SphereDisplay::updateTexture(ROSImageTexture*& texture)
{
	if(!texture)
	{
		return;
	}

	Ogre::String texture_name = texture->getTexture()->getName();

	try
	{
		texture->update();
	}
	catch (UnsupportedImageEncoding& e)
	{
		setStatus(StatusProperty::Error, "Front camera image", e.what());
		ROS_ERROR("SphereDisplay::updateTexture[%s]: UnsupportedImageEncoding: %s", texture_name.c_str(), e.what());
		return;
	}
	// RViz ROSImageTexture successfully updated


//	if(sphere_material_.isNull())
//	{
//		ROS_ERROR("SphereDisplay::updateTexture[%s]: sphere_material_ is NULL!", texture_name.c_str());
//		return;
//	}

	// Create Ogre textureUnitState if one does not exist already.
//	Ogre::Pass* pass = sphere_material_->getTechnique(0)->getPass(0);
//	//	ROS_ERROR("%s numStates() %d",texture_name.c_str(),pass->getNumTextureUnitStates());
//	if(!pass->getTextureUnitState(texture_name))
//	{
//		ROS_ERROR("SphereDisplay::updateTexture[%s]: texture_unit_state is NULL!", texture_name.c_str());
//		return;
//	}
	//	Ogre::TextureUnitState* unit_state = pass->getTextureUnitState(0);
	//	unit_state->setName(texture_name);
	//	unit_state->setTexture(texture->getTexture());
		//unit_state->setTextureAddressingMode(Ogre::TextureUnitState::TAM_BORDER);
		//unit_state->setTextureBorderColour(Ogre::ColourValue(0,0,0,0));
		//unit_state->setTextureScale(debug_property_->getFloat(),1);
		//unit_state->setTextureScroll(0.5,0);
//
//		ROS_INFO("CREATED TEXTURE UNIT STATE: %s",unit_state->getName().c_str());
//		ROS_INFO("ID: %d",pass->getTextureUnitStateIndex(unit_state));
//		if(pass->getTextureUnitStateIndex(unit_state)==1)
//		{
//			unit_state->setTextureScroll(-0.5,0);
//			ROS_INFO("UNIT STATE: %s",unit_state->getName().c_str());
//			unit_state->setColourOperation(Ogre::LBO_ALPHA_BLEND);
//		}
}

void SphereDisplay::imageToTexture(ROSImageTexture*& texture, const sensor_msgs::Image::ConstPtr& msg)
{
	cv_bridge::CvImagePtr cv_ptr;

	// simply converting every image to RGBA
	try
	{
		cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::RGBA8);
	}
	catch (cv_bridge::Exception& e)
	{
		ROS_ERROR("SphereDisplay: cv_bridge exception: %s", e.what());
		return;
	}

	// add completely white transparent border to the image so that it won't replicate colored pixels all over the mesh
	// cv::Scalar value(255, 255, 255, 0);
	//cv::copyMakeBorder(cv_ptr->image, cv_ptr->image, 1, 1, 1, 1, cv::BORDER_CONSTANT, value);
	//cv::flip(cv_ptr->image, cv_ptr->image, -1);
	bool is_front_texture = (texture == texture_front_);

	if (texture)
	{
		texture->addMessage(cv_ptr->toImageMsg());
	}
	else
	{
		//no texture, try to create one and add to texture_unit_state
		texture = new ROSImageTexture();
		if(!texture)
		{
			ROS_ERROR("Failed to create new texture.");
			return;
		}
		ROS_INFO("imageToTexture(): Created new texture: %s", texture->getTexture()->getName().c_str());

		if(sphere_material_.isNull())
		{
			ROS_ERROR("imageToTexture(): sphere_material_ is NULL.");
			return;
		}

		Ogre::Pass* pass = sphere_material_ -> getTechnique(0) -> getPass(0);
		if (!pass)
		{
			ROS_ERROR("imageToTexture(): pass is NULL.");
			return;
		}

		// try to get apropriate unit state (0-front cam, 1-rear cam)
		Ogre::TextureUnitState* unit_state = pass -> getTextureUnitState(is_front_texture ? 0 : 1 );
		if (!unit_state)
		{
			ROS_ERROR("Failed to get TextureUnitState.");
			return;
		}

		unit_state -> setTexture(texture -> getTexture());
		pass->getFragmentProgram()->escalateLoading();
		pass->getFragmentProgram()->reload();

	}
}

void SphereDisplay::reset()
{
//  Display::reset();
//  clear();
}

}  // namespace rviz




#include <pluginlib/class_list_macros.h>
PLUGINLIB_EXPORT_CLASS(rviz::SphereDisplay, rviz::Display)
