#include <ros/ros.h>
#include<map_creator/remove_reachability.h>
#include<octomap/OcTreeBaseImpl.h>

remove_obstacles_reachability::remove_obstacles_reachability():scene_rcvd_(false)
{
  subscriber_planning_scene_ = nh_.subscribe("/move_group/monitored_planning_scene",1,&remove_obstacles_reachability::readPlanningScene,this);
  map_rcvd_ = false;
  subscriber_reachability_ = nh_.subscribe("/reachability_map",1, &remove_obstacles_reachability::readMap, this);
  pub_filtered_reachability_ = nh_.advertise<map_creator::WorkSpace>("/reachability_map_filtered", 0);
  pub_colliding_reachability_ = nh_.advertise<map_creator::WorkSpace>("reachability_map_colliding", 0);
}

remove_obstacles_reachability::~remove_obstacles_reachability()
{
  ROS_INFO("Shutting down remove obstacles reachability");
  ros::shutdown();
}

void remove_obstacles_reachability::readPlanningScene(const moveit_msgs::PlanningScene scene_msg)
{
  scene_rcvd_ = false;
  if(scene_msg.world.octomap.octomap.data.size() > 0)
  {
    ROS_INFO("Planning scene received");
    octomap_msgs::Octomap octomap = scene_msg.world.octomap.octomap;
    octomap::AbstractOcTree* abstract_tree = octomap_msgs::msgToMap(octomap);
    collision_octree_ = (octomap::OcTree*) abstract_tree;
    scene_rcvd_ = true;
  }
}

void remove_obstacles_reachability::readMap(const map_creator::WorkSpace msg)
{
  ROS_INFO_STREAM( "Reachability Map Received! Number of reachability spheres: "<<msg.WsSpheres.size() );
  reachability_map_ = msg;
  reachability_resolution_ = msg.resolution;
  map_rcvd_= true;
  subscriber_reachability_.shutdown();
}



void remove_obstacles_reachability::createObstaclesPointCloud(octomap::OcTree &tree, pcl::PointCloud<pcl::PointXYZ>::Ptr obstacle_vertices)
{
  unsigned int max_depth = tree.getTreeDepth();
  if( !obstacle_vertices->empty() )
  {
      obstacle_vertices->clear();
  }
  // Create set of occupied voxel centers (and vertices) at max depth
  std::set<std::vector<double> > points_set;
  points_set.clear();
  for(octomap::OcTree::leaf_iterator it = tree.begin_leafs(max_depth), end = tree.end_leafs(); it !=end; ++it)
  {

    if(tree.isNodeOccupied(*it))
    {
      double voxel_size = it.getSize();
      for(int dx = -1; dx<=1;++dx)
      {
        for(int dy = -1; dy<=1;++dy)
        {
          for(int dz = -1; dz<=1;++dz)
          {
            std::vector<double> point;
            std::vector<double> point_center;
            point_center.push_back(it.getX());
            point_center.push_back(it.getY());
            point_center.push_back(it.getZ());
            points_set.insert(point_center);
            point.push_back(it.getX()+(dx*voxel_size/2));
            point.push_back(it.getY()+(dy*voxel_size/2));
            point.push_back(it.getZ()+(dz*voxel_size/2));
            points_set.insert(point);
          }
        }
      }
    }
  }

  ROS_INFO_STREAM("Number of vertices in obstacle point cloud: "<<points_set.size() );

  for(std::set<std::vector<double> >::iterator it = points_set.begin(); it !=points_set.end(); ++it)
  {
    pcl::PointXYZ cloud_point;
    cloud_point.x = (*it)[0];
    cloud_point.y = (*it)[1];
    cloud_point.z = (*it)[2];
    obstacle_vertices->push_back(cloud_point);
  }
}

void remove_obstacles_reachability::createFilteredReachability(filterType type, pcl::octree::OctreePointCloudSearch<pcl::PointXYZ> &search_tree, map_creator::WorkSpace &filtered_map, map_creator::WorkSpace &colliding_map)
{
  filtered_map.header = reachability_map_.header;
  filtered_map.resolution = reachability_map_.resolution;
  colliding_map.header = reachability_map_.header;
  colliding_map.resolution = reachability_map_.resolution;

  double circumscribe_reachability_radius = sqrt(3)* reachability_resolution_/2.0;
  double inscribe_reachability_radius = reachability_resolution_/2.0;
  for(int i=0;i<reachability_map_.WsSpheres.size();++i)
  {
    geometry_msgs::Point32 voxel_center = reachability_map_.WsSpheres[i].point;
    pcl::PointXYZ search_point;
    search_point.x = voxel_center.x;
    search_point.y = voxel_center.y;
    search_point.z = voxel_center.z;
    std::vector<int> point_idx_vec;
    std::vector<float> point_sqrd_dis;

    switch(type)
    {
    case remove_obstacles_reachability::VOXEL:
    {
      search_tree.voxelSearch(search_point, point_idx_vec);
      break;
    }
    case remove_obstacles_reachability::INSCRIBED_SPHERE:
    {
      search_tree.radiusSearch(search_point, inscribe_reachability_radius, point_idx_vec, point_sqrd_dis);
      break;
    }
    case remove_obstacles_reachability::CIRCUMSCRIBED_SPHERE:
    {
      search_tree.radiusSearch(search_point, circumscribe_reachability_radius, point_idx_vec, point_sqrd_dis);
      break;
    }
   }

    if(point_idx_vec.size() == 0)
    {
      filtered_map.WsSpheres.push_back(reachability_map_.WsSpheres[i]);
    }
    else
    {
      colliding_map.WsSpheres.push_back(reachability_map_.WsSpheres[i]);
    }

  }
  ROS_INFO("Reachability Map Filtered!");
  ROS_INFO_STREAM( "Number of colliding voxels: "<<colliding_map.WsSpheres.size() );
  ROS_INFO_STREAM( "Number of spheres remaining: "<<filtered_map.WsSpheres.size() );
}

void remove_obstacles_reachability::spin(filterType filter_type)
{
  ros::Rate loop_rate(SPIN_RATE);
  pcl::PointCloud<pcl::PointXYZ>::Ptr obstacles_cloud(new pcl::PointCloud <pcl::PointXYZ>);
  while(ros::ok())
  {
    if (scene_rcvd_ )
    {
      ROS_INFO("Received new scene");
      createObstaclesPointCloud(*collision_octree_, obstacles_cloud);
      std::cout<<"Size of obstacles_cloud: "<<obstacles_cloud->points.size()<<std::endl;
    }

    else if(!map_rcvd_)
    {
      ROS_WARN("Awaiting reachability map");
    }

    if(map_rcvd_ && obstacles_cloud->points.size()!=0)
    {
      std::chrono::high_resolution_clock::time_point t_start = std::chrono::high_resolution_clock::now();
      pcl::octree::OctreePointCloudSearch<pcl::PointXYZ> obstacles_tree(reachability_resolution_);
      obstacles_tree.setInputCloud(obstacles_cloud);
      obstacles_tree.addPointsFromInputCloud();

      map_creator::WorkSpace filtered_map;
      map_creator::WorkSpace colliding_map;
      createFilteredReachability(filter_type, obstacles_tree, filtered_map, colliding_map);

      std::chrono::high_resolution_clock::time_point t_end = std::chrono::high_resolution_clock::now();
      std::chrono::milliseconds ms = std::chrono::duration_cast <std::chrono::milliseconds>(t_end-t_start);
      ROS_INFO_STREAM("Time requierd to process map: "<<ms.count()<<"ms\n");

      pub_colliding_reachability_.publish(colliding_map);
      pub_filtered_reachability_.publish(filtered_map);

    }

    ros::spinOnce();
    loop_rate.sleep();
  }
}


int main(int argc, char **argv)
{
  remove_obstacles_reachability::filterType filter_type;
  std::cout<<"argc: "<<argc<<'\n';

  if(argc == 1)
    {
      ROS_INFO("No filter type provided. Defaulting to CIRCUMSCRIBED SPHERE!");
      filter_type = remove_obstacles_reachability::CIRCUMSCRIBED_SPHERE;
    }
    else
    {
      std::string input = argv[1];
      std::transform(input.begin(), input.end(), input.begin(), ::tolower);

      if(input.compare("voxel") == 0)
      {
        ROS_INFO("Setting filter type to VOXEL");
        filter_type = remove_obstacles_reachability::VOXEL;
      }
      else if(input.compare("circumscribe") == 0)
      {
        ROS_INFO("Setting filter type to CIRCUMSCRIBED SPHERE");
        filter_type = remove_obstacles_reachability::CIRCUMSCRIBED_SPHERE;
      }
      else if(input.compare("inscribe") == 0)
      {
        ROS_INFO("Setting filter type to INSCRIBED SPHERE");
        filter_type = remove_obstacles_reachability::INSCRIBED_SPHERE;
      }
      else
      {
        ROS_ERROR_STREAM("Invalid filtering type "<<input<<" receievd. Shutting Down!");
        ros::shutdown();
      }
  }


  ros::init(argc, argv, "remove_reachability");
  ros::AsyncSpinner spinner(1);
  spinner.start();
  remove_obstacles_reachability rm;
  rm.spin(filter_type);
}