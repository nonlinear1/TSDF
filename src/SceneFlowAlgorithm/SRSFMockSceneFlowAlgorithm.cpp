#include "SRSFMockSceneFlowAlgorithm.hpp"
#include "FileUtilities.hpp"

#include <iostream>
#include <regex>

SRSFMockSceneFlowAlgorithm::SRSFMockSceneFlowAlgorithm( const std::string & scene_flow_directory_name ) {
	// Check directory exists
	bool is_directory;
	if( file_exists( scene_flow_directory_name, is_directory) && is_directory) {
		// Stash this
		m_directory = scene_flow_directory_name;

		// Count the mnumber of scene flow files of the form sflow_nnnn.xml
		files_in_directory( m_directory, m_scene_flow_file_names, []( std::string name ) {
			// If name is color_nnnnn.pgm or depth.ppm then we accept it
			bool is_valid = false;

			try {
				std::regex scene_flow_filename_regex( "sflow_[0-9]{5}\\.xml", std::regex_constants::icase );

				is_valid = std::regex_match( name, scene_flow_filename_regex );
    		} catch (const std::regex_error& e) {
        		std::cerr << "Invalid regex in SRSFMockSceneFlowAlgorithm: " << e.what() << '\n';
		    }

			return is_valid;
		});

		// Sort them
		std::sort( m_scene_flow_file_names.begin(), m_scene_flow_file_names.end() );

		// And set index to first entry
		m_current_file_index = 0;

	} else {
		std::cerr << "Couldn't find directory " << scene_flow_directory_name << std::endl;
	}
}
/**
 * Given a string, parse it into the specified number of floats
 * @param string The source string
 * @param numFloats The number of floats to parse
 * @param readValues A pointer into which to store the values of floats read
 * @return true if the number of floats werer read successfully, otherwise false
 */
bool SRSFMockSceneFlowAlgorithm::read_floats_from_string( const char * string, uint num_floats, float * read_values) {
	bool read_ok = true;

	if ( !read_values ) {
		std::cerr << "Warning: read_values is null in read_floats_from_string. Values will be counted but not returned" << std::endl;
	}

	if ( num_floats == 0 ) {
		std::cerr << "Warning: num_floats is 0 in read_floats_from_string. This is valid but probably an error" << std::endl;
		return true;
	}


	if ( string ) {
		std::istringstream iss( string );
		for ( size_t i = 0; i < num_floats; i++ ) {
			if ( iss.good() ) {
				iss >> read_values[i];
			} else {
				std::cerr << "Not enough float entries in string. Expected " << num_floats << ", read " << (i + 1) << std::endl;
				read_ok = false;
				break;
			}
		}
	} else {
		std::cerr << "String is null in readFloatsFromString" << std::endl;
		read_ok = false;
	}
	return read_ok;
}

/**
 * Read the residuals node from an SRSF xml document
 * @param doc Teh document
 * @param nodeName The name of the node to read (SFx, SFy or SFz usually)
 * @param width The number of columns in the data
 * @param height The number of rows in the data
 * @return true if the number of floats werer read successfully, otherwise false
 */
float * SRSFMockSceneFlowAlgorithm::read_residuals_node( const TiXmlDocument & doc, const char * node_name, uint32_t & width, uint32_t & height ) {
	if ( ! node_name ) {
		std::cerr << "Error: missing node name in read_residuals_node" << std::endl;
		return nullptr;
	}

	float * values = nullptr;

	// Find the node's rows and columns elements
	const TiXmlElement * row_element = doc.RootElement()->FirstChild( node_name )->FirstChildElement( "rows" );
	const TiXmlElement * col_element = doc.RootElement()->FirstChild( node_name )->FirstChildElement( "cols" );

	const char * row_text = row_element->GetText( );
	width = atoi( row_text);
	const char * col_text = col_element->GetText( );
	height = atoi( col_text );

	size_t num_entries = width * height;
	if ( num_entries) {
		values = new float[ num_entries];
		if ( values ) {
			const TiXmlElement * data_element = doc.RootElement()->FirstChild( node_name )->FirstChildElement( "data" );
			if ( data_element) {
				const char * string = data_element->GetText();
				if ( ! read_floats_from_string( string, num_entries, values ) ) {
					std::cerr << "Error: Problem reading values from residuals node " << node_name << std::endl;
				}
			} else {
				std::cerr << "Error: No data node for residuals node " << node_name << std::endl;
			}
		} else {
			std::cerr << "Error: Out of memory allocating storage for " << num_entries << " entries in residuals node " << node_name << std::endl;
		}
	} else {
		std::cerr << "Error: Number of entries in residuals node " << node_name << " is zero " << std::endl;
	}
	return values;
}



/**
 * Read the scene flow data from the given file
 * @param fileName The name of XML file
 * @param translation The global translation. Set by this method
 * @param rotation The global translation. Set by this method
 * @param residuals The residual translation per pixel
 * @return true if the data were read correctly
 */
bool SRSFMockSceneFlowAlgorithm::read_scene_flow( const std::string & file_name, Eigen::Vector3f& translation, Eigen::Vector3f& rotation, Eigen::Matrix<float, 3, Eigen::Dynamic>& residuals) {
	bool read_ok = true;

	// Load file using TinyXML
	TiXmlDocument doc;
	if ( !doc.LoadFile( file_name.c_str() ) ) {
		read_ok = false;
		std::cerr << "Error reading scene flow file " << file_name << std::endl;
	}

	// Extract rigid body transformatiion
	// Get the global translation
	if ( read_ok ) {
		TiXmlElement * element = doc.RootElement()->FirstChild( "Translation" )->FirstChildElement( "data" );
		if ( element ) {
			const char *trans_vec_string = element->GetText();
			float t[3];
			if ( read_floats_from_string( trans_vec_string, 3, t ) ) {
				translation[0] = t[0];
				translation[1] = t[1];
				translation[2] = t[2];
				std::cout << "Translation (" << translation << ")" << std::endl;
			} else {
				read_ok = false;
				std::cerr << "Missing content in 'Translation' node in " << file_name << std::endl;
			}
		} else {
			read_ok = false;
			std::cerr << "Couldn't find 'Translation' node in " << file_name << std::endl;
		}
	}


	// Get the global rotation
	if ( read_ok ) {
		TiXmlElement * element = doc.RootElement()->FirstChild( "Rotation" )->FirstChildElement( "data" );
		if ( element ) {
			const char *rot_vec_string = element->GetText();
			float r[3];
			if ( read_floats_from_string( rot_vec_string, 3, r ) ) {
				rotation[0] = r[0];
				rotation[1] = r[1];
				rotation[2] = r[2];
			} else {
				read_ok = false;
				std::cerr << "Missing content in 'Rotation' node in " << file_name << std::endl;
			}
		} else {
			read_ok = false;
			std::cerr << "Couldn't find 'Rotation' node in " << file_name << std::endl;
		}
	}

	uint32_t width = 0, height = 0;
	float * residual_x = nullptr;
	if ( read_ok ) {
		// Get the X residucals nodes
		residual_x = read_residuals_node( doc, "SFx", width, height );
		if ( ! residual_x) {
			read_ok = false;
			std::cerr << "Couldn't read X residuals in " << file_name << std::endl;
		}
	}

	float * residual_y = nullptr;
	if ( read_ok ) {
		// Get the Y residucals nodes
		uint32_t y_width = 0, y_height = 0;
		residual_y = read_residuals_node( doc, "SFy", y_width, y_height );
		if ( ( ! residual_y ) || ( y_width != width) || ( y_height != height) ) {
			read_ok = false;
			delete [] residual_x;
			std::cerr << "Couldn't read Y residuals in " << file_name << std::endl;
		}
	}

	float * residual_z = nullptr;
	if ( read_ok ) {
		// Get the Y residucals nodes
		uint32_t z_width = 0, z_height = 0;
		residual_z = read_residuals_node( doc, "SFz", z_width, z_height );
		if ( ( ! residual_z ) || ( z_width != width) || ( z_height != height) ) {
			read_ok = false;
			delete [] residual_x;
			delete [] residual_y;
			std::cerr << "Couldn't read Z residuals in " << file_name << std::endl;
		}
	}

	if ( read_ok ) {
		size_t   num_values = width * height;

		residuals.resize( 3, num_values);
		for ( size_t i = 0; i < num_values; i++ ) {

			residuals(0, i) = residual_x[i];
			residuals(1, i) = residual_y[i];
			residuals(2, i) = residual_z[i];

			if ( residual_x[i] != 0 || residual_y[i] != 0 || residual_z[i] != 0 ) {
				std::cout << "(" << residual_x[i] << "," << residual_y[i] << "," << residual_z[i] << ")";
			}
		}
	}
	return read_ok;
}




/**
 * Compute the scene flow from previous and current colour and depth images
 */
void SRSFMockSceneFlowAlgorithm::compute_scene_flow(	const DepthImage * pDepthImage,
        const PngWrapper * pColourImage,
        Eigen::Vector3f&   						translation,
        Eigen::Vector3f&   						rotation,
        Eigen::Matrix<float, 3, Eigen::Dynamic>& residuals ) {
	if( m_current_file_index < m_scene_flow_file_names.size() ) {
		std::string path_to_file = m_directory + "/" + m_scene_flow_file_names[m_current_file_index];
		// Read the file
		if (!read_scene_flow( path_to_file, translation, rotation, residuals ) ) {
			std::cerr << "Failed to read scene flow from file " << path_to_file << std::endl;
		} else {
			m_current_file_index ++;
		}
	} else {
		std::cerr << "Tried to read scene flow file that doesn't exist" << std::endl;
	}
}