#include "VSFilter.hpp"

#include <opencv2/opencv.hpp>
#include <opencv2/core/ocl.hpp>

#include <util/platform.h>

namespace lvk
{

	//===================================================================================
	//		CONSTANT PROPERTIES/SETTINGS
	//===================================================================================

	static const FrameTracker::Properties TRACKING_PROPERTIES = {/* Use defaults */};

	static constexpr auto PROP_SMOOTHING_RADIUS = "SMOOTH_RADIUS";
	static constexpr auto SMOOTHING_RADIUS_DEFAULT = 14;
	static constexpr auto SMOOTHING_RADIUS_MIN = 2;
	static constexpr auto SMOOTHING_RADIUS_MAX = 30;

	static constexpr auto PROP_FRAME_DELAY_INFO = "FRAME_DELAY_INFO";
	static constexpr auto FRAME_DELAY_INFO_MIN = 0;
	static constexpr auto FRAME_DELAY_INFO_MAX = 100 * SMOOTHING_RADIUS_MAX;

	static constexpr auto PROP_CROP_PERCENTAGE = "CROP_PERCENTAGE";
	static constexpr auto CROP_PERCENTAGE_DEFAULT = 5;
	static constexpr auto CROP_PERCENTAGE_MIN = 1;
	static constexpr auto CROP_PERCENTAGE_MAX = 25;

	static constexpr auto PROP_TEST_MODE = "TEST_MODE";
	static constexpr auto TEST_MODE_DEFAULT = false;

	//===================================================================================
	//		FILTER IMPLEMENTATION
	//===================================================================================

	obs_properties_t* VSFilter::Properties()
	{
		obs_properties_t* properties = obs_properties_create();

		// Slider for window radius.
		auto property = obs_properties_add_int(
				properties,
				PROP_SMOOTHING_RADIUS,
				"Smoothing Radius",
				SMOOTHING_RADIUS_MIN,
				SMOOTHING_RADIUS_MAX,
				2
		);

		property = obs_properties_add_int(
				properties,
				PROP_FRAME_DELAY_INFO,
				"Frame Delay",
				FRAME_DELAY_INFO_MIN,
				FRAME_DELAY_INFO_MAX,
				1
		);
		obs_property_int_set_suffix(property, "ms");
		obs_property_set_enabled(property, false);

		// Slider for total proportion of allowable crop along each dimension.
		property = obs_properties_add_int_slider(
				properties,
				PROP_CROP_PERCENTAGE,
				"Crop",
				CROP_PERCENTAGE_MIN,
				CROP_PERCENTAGE_MAX,
				1
		);
		obs_property_int_set_suffix(property, "%");


		// Toggle for test mode, used to help configure settings.
		property = obs_properties_add_bool(
				properties,
				PROP_TEST_MODE,
				"Test Mode"
		);

		return properties;
	}

	//-------------------------------------------------------------------------------------

	void VSFilter::LoadDefault(obs_data_t* settings)
	{
		obs_data_set_default_int(settings, PROP_SMOOTHING_RADIUS, SMOOTHING_RADIUS_DEFAULT);
		obs_data_set_default_int(settings, PROP_CROP_PERCENTAGE, CROP_PERCENTAGE_DEFAULT);
		obs_data_set_default_bool(settings, PROP_TEST_MODE, TEST_MODE_DEFAULT);
	}

	//-------------------------------------------------------------------------------------

	VSFilter* VSFilter::Create(obs_source_t* context)
	{
		auto filter = new VSFilter(context);

		if(!filter->validate())
		{
			delete filter;
			return nullptr;
		}

		// Configure OpenCV to use OpenCL
		cv::ocl::setUseOpenCL(true);

		return filter;
	}

	//-------------------------------------------------------------------------------------

	VSFilter::VSFilter(obs_source_t* context)
		: m_Context(context),
		  m_Shader(nullptr),
		  m_CropParam(nullptr),
		  m_TestMode(false),
		  m_CropProportion(0),
		  m_SmoothingRadius(0),
		  m_WarpFrame(cv::UMatUsageFlags::USAGE_ALLOCATE_DEVICE_MEMORY),
		  m_TrackingFrame(cv::UMatUsageFlags::USAGE_ALLOCATE_DEVICE_MEMORY),
		  m_FrameTracker(TRACKING_PROPERTIES)
	{

		char* shader_path = obs_module_file("effects/vs.effect");
		if(shader_path != nullptr)
		{
			obs_enter_graphics();

			m_Shader = gs_effect_create_from_file(shader_path, nullptr);
			bfree(shader_path);

			if(m_Shader)
				m_CropParam = gs_effect_get_param_by_name(m_Shader, "crop_proportion");

			obs_leave_graphics();
		}
	}

	//-------------------------------------------------------------------------------------

	VSFilter::~VSFilter()
	{
		reset_buffers();
	}

	//-------------------------------------------------------------------------------------

	void VSFilter::configure(obs_data_t* settings)
	{
		const uint32_t new_radius = round_even(obs_data_get_int(settings, PROP_SMOOTHING_RADIUS));

		if(m_SmoothingRadius != new_radius)
			prepare_buffers(new_radius);

		obs_video_info video_info;
		obs_get_video_info(&video_info);
		const float frame_ms = 1000.0 * video_info.fps_den / video_info.fps_num;

		const uint32_t frame_delay = obs_data_get_int(settings, PROP_FRAME_DELAY_INFO);
		const uint32_t new_frame_delay = frame_ms * m_FrameQueue.window_size();

		if(frame_delay != new_frame_delay)
		{
			obs_data_set_int(settings, PROP_FRAME_DELAY_INFO, frame_ms * m_FrameQueue.window_size());
			obs_source_update_properties(m_Context);
		}

		m_CropProportion = obs_data_get_int(settings, PROP_CROP_PERCENTAGE) / 100.0f;

		m_TestMode = obs_data_get_bool(settings, PROP_TEST_MODE);
	}

	//-------------------------------------------------------------------------------------

	void VSFilter::tick()
	{
		if(stabilisation_ready())
		{
			auto frame_size = m_FrameQueue.oldest().frame.size();

			const uint32_t total_horz_crop = frame_size.width * m_CropProportion;
			const uint32_t total_vert_crop = frame_size.height * m_CropProportion;

			m_CropRegion.x = total_horz_crop / 2;
			m_CropRegion.y = total_vert_crop / 2;
			m_CropRegion.width = frame_size.width - total_horz_crop;
			m_CropRegion.height = frame_size.height - total_vert_crop;

			if(m_TestMode)
				m_OutputSize = frame_size;
			else
				m_OutputSize = m_CropRegion.size();
		}
	}

	//-------------------------------------------------------------------------------------

	void VSFilter::render() const
	{
		if(!obs_source_process_filter_begin(m_Context, GS_RGBA, OBS_NO_DIRECT_RENDERING))
				return;

		if(m_TestMode)
			gs_effect_set_float(m_CropParam, 0.0f);
		else
			gs_effect_set_float(m_CropParam, m_CropProportion);

		obs_source_process_filter_end(m_Context, m_Shader, m_OutputSize.width, m_OutputSize.height);
	}

	//-------------------------------------------------------------------------------------

	obs_source_frame* VSFilter::process(obs_source_frame* obs_frame)
	{
		const uint64_t start_time = os_gettime_ns();

		auto& buffer = m_FrameQueue.advance();
		buffer.frame << obs_frame;
		buffer.output = obs_frame;

		cv::extractChannel(buffer.frame, m_TrackingFrame, 0);

		auto& motion = m_Trajectory.advance();
		motion.velocity = m_FrameTracker.track(m_TrackingFrame);
		motion.displacement = m_Trajectory.previous().displacement + motion.velocity;

		if(stabilisation_ready())
		{
			const auto& [frame, output] = m_FrameQueue.oldest();
			const auto& [displacement, velocity] = m_Trajectory.centre();

			const auto correction = m_Trajectory.convolve(m_Filter).displacement - displacement;
			const auto smooth_warp = velocity + correction;
			const auto cropped_warp = enclose_crop(frame, smooth_warp);

			cv::warpAffine(frame, m_WarpFrame, cropped_warp.as_matrix(), frame.size());

			m_WarpFrame >> output;

			const uint64_t end_time = os_gettime_ns();

			if(m_TestMode)
				draw_test_mode(m_WarpFrame, end_time - start_time) >> output;

			return output;
		}

		return nullptr;
	}

	//-------------------------------------------------------------------------------------

	Transform VSFilter::enclose_crop(const cv::UMat& frame, const Transform& transform)
	{
		// Reduce the magnitude of the transform until the crop region is
		// fully enclosed within the warped frame. We reduce the magnitude
		// by iteratively lerping the transform back to identity in small steps.

		constexpr double max_t = 1.0;
		constexpr int max_iterations = 100;
		constexpr double step = max_t/max_iterations;
		const auto identity = Transform::Identity();

		double t = step;
		auto reduced_transform = transform;
		BoundingBox frame_bounds(frame.size(), reduced_transform);
		while(t <= max_t && !frame_bounds.encloses(m_CropRegion))
		{
			reduced_transform = lerp(transform, identity, t);
			frame_bounds.transform(reduced_transform);
			t += step;
		}

		return reduced_transform;
	}

	//-------------------------------------------------------------------------------------

	cv::UMat VSFilter::draw_test_mode(cv::UMat& frame, const uint64_t frame_time_ns)
	{
		const double frame_time_ms = frame_time_ns * 1.0e-6;

		//TODO: switch to C++20 fmt as soon as GCC supports it.
		std::stringstream text;
		text << std::fixed << std::setprecision(2);
		text << frame_time_ms << "ms";

		const cv::Scalar magenta_yuv(105, 212, 234);
		cv::rectangle(frame, m_CropRegion, magenta_yuv, 2);
		cv::putText(frame, text.str(), m_CropRegion.tl() + cv::Point(5, 40), cv::FONT_HERSHEY_DUPLEX, 1.5, magenta_yuv, 2);

		return frame;
	}

	//-------------------------------------------------------------------------------------

	void VSFilter::prepare_buffers(const uint32_t smoothing_radius)
	{
		LVK_ASSERT(smoothing_radius >= SMOOTHING_RADIUS_MIN && smoothing_radius % 2 == 0);

		// NOTE: Stabalisation is achieved by applying a windowed low pass filter
		// to the frame/camera's path to remove high frequency 'shaking'. Effective
		// filtering requires a full sized window which takes into account both past
		// and future frames, obtained by delaying the stream. Delay is introduced
		// via half-sized sliding buffers. Such that the oldest element corresponds
		// with the centre element in the full sized path buffer.

		m_SmoothingRadius = smoothing_radius;
		const uint32_t queue_size = smoothing_radius + 2;
		const uint32_t window_size = 2 * smoothing_radius + 1;

		//TODO: causes memory leak if smaller then before, because frames are dropped!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		m_FrameQueue.resize(queue_size);
		m_Trajectory.resize(window_size);
		m_Filter.resize(window_size);

		// We use a low pass Gaussian filter because it has both decent time domain
		// and frequency domain performance. Unlike an average or windowed sinc filter.
		// As a rule of thumb, sigma is chosen to fit 99.7% of the distribution in the window.
		const auto gaussian_kernel = cv::getGaussianKernel(window_size, window_size/6.0);

		m_Filter.clear();
		for(uint32_t i = 0; i < window_size; i++)
			m_Filter.push(gaussian_kernel.at<double>(i));

		// Enforces synchronisation.
		reset_buffers();
	}

	//-------------------------------------------------------------------------------------

	void VSFilter::reset()
	{
		reset_buffers();
		m_FrameTracker.reset();
	}

	//-------------------------------------------------------------------------------------

	void VSFilter::reset_buffers()
	{
		LVK_ASSERT(m_Trajectory.window_size() > m_FrameQueue.window_size());

		// Need to release all the OBS frames to prevent memory leaks
		obs_source_t* parent = obs_filter_get_parent(m_Context);
		for(uint32_t i = 0; i < m_FrameQueue.elements(); i++)
			obs_source_release_frame(parent, m_FrameQueue[i].output);

		m_FrameQueue.clear();
		m_Trajectory.clear();

		// The vector data for the oldest frame in the frame queue should
		// be synchronised so that it is always at the centre of the trajectory.
		// The frame tracker always gives the vector from the previous to current
		// frame, while we want the vector from the current to the next frame instead.
		// So lag the tracjectory by one.
		m_Trajectory.advance(Transform::Identity());
		while(m_Trajectory.elements() < m_SmoothingRadius - 1)
			m_Trajectory.advance(m_Trajectory.newest() + Transform::Identity());

	}

	//-------------------------------------------------------------------------------------

	bool VSFilter::stabilisation_ready() const
	{
		LVK_ASSERT(m_Trajectory.full() == m_FrameQueue.full());

		return m_Trajectory.full()
			&& m_FrameQueue.full();
	}

	//-------------------------------------------------------------------------------------

	uint32_t VSFilter::width() const
	{
		return m_OutputSize.width;
	}

	//-------------------------------------------------------------------------------------

	uint32_t VSFilter::height() const
	{
		return m_OutputSize.height;
	}

	//-------------------------------------------------------------------------------------

	bool VSFilter::validate() const
	{
		// NOTE: Must run through OpenCL for performance reasons.
		return cv::ocl::haveOpenCL()
			&& m_Shader != nullptr
			&& m_CropParam != nullptr;
	}

	//-------------------------------------------------------------------------------------

	VSFilter::FrameBuffer::FrameBuffer()
		: frame(cv::UMatUsageFlags::USAGE_ALLOCATE_DEVICE_MEMORY),
		  output(nullptr)
	{}

	//-------------------------------------------------------------------------------------

	VSFilter::FrameVector::FrameVector(const Transform& displacement, const Transform& velocity)
		: displacement(displacement),
		  velocity(velocity)
	{}

	//-------------------------------------------------------------------------------------

	VSFilter::FrameVector VSFilter::FrameVector::operator+(const VSFilter::FrameVector& other) const
	{
		return FrameVector(displacement + other.displacement, velocity + other.velocity);
	}

	//-------------------------------------------------------------------------------------

	VSFilter::FrameVector VSFilter::FrameVector::operator*(const double scaling) const
	{
		return FrameVector(displacement * scaling, velocity * scaling);
	}

	//-------------------------------------------------------------------------------------

}
