#include <assert.h>
#include <boost/filesystem.hpp>
#include <cstring>
#include <iostream>
#include <map>
#include <unistd.h>
#include <stdexcept>

#include "pod5_format/c_api.h"

static pod5_error_t pod5_errno = POD5_OK;
static const char* err_string;
static std::unique_ptr<std::map<std::string, int16_t>> pore_type_cache;

#define DEBUG
#define MAX_END_REASON_STRING_SIZE (1 << 16)
#define MAX_PORE_TYPE_STRING_SIZE (1 << 16)

#define EXIT_PROGRAM_ON_FAIL(reader, writer)    \
	pod5_errno = pod5_get_error_no();           \
	if (pod5_errno != POD5_OK)                  \
	{                                           \
		err_string = pod5_get_error_string();   \
		std::cerr << err_string << "\n";        \
		free(curr_dir);                         \
		release_pod5_resources(reader, writer); \
		return EXIT_FAILURE;                    \
	}

#define LOG_PROGRAM_ERROR(pod5_errno_local)   \
	if (pod5_errno_local != POD5_OK)          \
	{                                         \
		err_string = pod5_get_error_string(); \
		std::cerr << err_string << "\n";      \
	}

#ifdef DEBUG
#define THROW_DEBUG_EXCEPTION(pod5_errno_local)\
	LOG_PROGRAM_ERROR(pod5_errno_local)\
	throw std::runtime_error("");
#else
#define	THROW_DEBUG_EXCEPTION(pod5_errno_local) LOG_PROGRAM_ERROR(pod5_errno_local)
#endif

#ifdef DEBUG
	#define DEBUG_FREE(ptr) \
		free(ptr); \
		ptr = nullptr;
#else
	#define DEBUG_FREE(ptr) \
		free(ptr);
#endif


void release_pod5_resources(Pod5FileReader_t *reader,
							Pod5FileWriter_t *writer)
{
	if (writer != NULL)
		pod5_close_and_free_writer(writer);
	if (reader != NULL)
		pod5_close_and_free_reader(reader);
	pod5_terminate();
}

bool is_path_relative(std::string path)
{
	return !path.empty() && path.front() != '/';
}

char *alloc_and_get_curr_dir_name()
{
#ifdef _GNU_SOURCE
	return get_current_dir_name();
#endif
#ifndef _GNU_SOURCE
	size_t size = 1024;
	char *buffer = (char *)malloc(sizeof(char) * size);
	size = size << 1;
	char *dummy = NULL;
	for (; dummy != NULL; size << 1)
	{
		free(buffer);
		buffer = (char *)malloc(sizeof(char) * size);
		dummy = getcwd(buffer, size);
	}
	return buff;
#endif
}

// Caller MUST release memory manually. end_reason_string_value is allocated
// by the function but end_reason_string_value_size and
// end_reason_value MUST be allocated by the caller.
void pod5_get_end_reason_wrapped(Pod5ReadRecordBatch_t *batch,
								 int16_t end_reason,
								 pod5_end_reason_t *end_reason_value,
								 char **end_reason_string_value,
								 size_t *end_reason_string_value_size)
{

	*end_reason_string_value_size = 16 << 4;
	*end_reason_string_value = (char *)malloc(sizeof(char) * *end_reason_string_value_size);
	pod5_error error = POD5_OK;
	do
	{
		error = pod5_get_end_reason(batch, end_reason, end_reason_value,
									*end_reason_string_value,
									end_reason_string_value_size);
		if (error == POD5_ERROR_STRING_NOT_LONG_ENOUGH)
		{
			*end_reason_string_value_size = *end_reason_string_value_size << 1;
			*end_reason_string_value = (char *) realloc(*end_reason_string_value, *end_reason_string_value_size);
		}
		else if (error != POD5_OK)
		{
			free(*end_reason_string_value);
			LOG_PROGRAM_ERROR(error)
			return;
		}
	} while (error != POD5_OK &&
			 *end_reason_string_value_size <= MAX_END_REASON_STRING_SIZE);
}

void pod5_get_pore_wrapped(Pod5ReadRecordBatch_t *batch, int16_t pore_type,
						   char **pore_type_string_value,
						   size_t *pore_type_string_value_size)
{
	*pore_type_string_value_size = 16 << 4;
	*pore_type_string_value =
		(char *)malloc(sizeof(char) * *pore_type_string_value_size);
	pod5_error error = POD5_OK;
	do
	{
		error = pod5_get_pore_type(batch, pore_type, *pore_type_string_value,
								   pore_type_string_value_size);
		if (error == POD5_ERROR_STRING_NOT_LONG_ENOUGH)
		{
			*pore_type_string_value_size = *pore_type_string_value_size << 1;
			*pore_type_string_value = (char *) realloc(*pore_type_string_value, *pore_type_string_value_size);
		}
		else if (error != POD5_OK)
		{
			free(*pore_type_string_value);
			*pore_type_string_value = nullptr;
			THROW_DEBUG_EXCEPTION(error);
			return;
		}
	} while (error != POD5_OK && *pore_type_string_value_size <= MAX_PORE_TYPE_STRING_SIZE);
}

/// \param[out] row_data: Transformed data in a format to be passed to a writer.
///                       Memory is alloc'd by the function.
///                       Memory must be freed by the caller using
///                       free_batch_array in_data memory must not be freed
///                       until calling free_batch_array 
void transform_read_data_batch_array(
	const ReadBatchRowInfo_t *in_data, const size_t record_count,
	Pod5ReadRecordBatch_t *batch, Pod5FileWriter_t *writer, ReadBatchRowInfoArray_t *row_data)
{
	// Inner arrays alloc (using alliases to avoid const-correcness enforcement by
	// the compiler and thus being able to actually write a
	// ReadBatchRowInfoArray_t)
	read_id_t *read_id = (read_id_t *)malloc(sizeof(read_id_t) * record_count);
	uint32_t *read_number = (uint32_t *)malloc(sizeof(uint32_t) * record_count);
	uint64_t *start_sample = (uint64_t *)malloc(sizeof(uint64_t) * record_count);
	float *median_before = (float *)malloc(sizeof(float) * record_count);
	uint16_t *channel = (uint16_t *)malloc(sizeof(uint16_t) * record_count);
	uint8_t *well = (uint8_t *)malloc(sizeof(uint8_t) * record_count);
	int16_t *pore_type = (int16_t *)malloc(sizeof(uint16_t) * record_count);
	float *calibration_offset = (float *)malloc(sizeof(float) * record_count);
	float *calibration_scale = (float *)malloc(sizeof(float) * record_count);
	pod5_end_reason_t *end_reason =
		(pod5_end_reason_t *)malloc(sizeof(pod5_end_reason_t) * record_count);
	uint8_t *end_reason_forced =
		(uint8_t *)malloc(sizeof(uint8_t) * record_count);
	int16_t *run_info_id = (int16_t *)malloc(sizeof(int16_t) * record_count);
	uint64_t *num_minknow_events =
		(uint64_t *)malloc(sizeof(uint64_t) * record_count);
	float *predicted_scaling_scale =
		(float *)malloc(sizeof(float) * record_count);
	float *predicted_scaling_shift =
		(float *)malloc(sizeof(float) * record_count);
	float *tracked_scaling_scale = (float *)malloc(sizeof(float) * record_count);
	float *tracked_scaling_shift = (float *)malloc(sizeof(float) * record_count);
	uint32_t *num_reads_since_mux_change =
		(uint32_t *)malloc(sizeof(uint32_t) * record_count);
	float *time_since_mux_change = (float *)malloc(sizeof(float) * record_count);

	// Create alliases to pointers in row_data
	row_data->read_id = read_id;
	row_data->read_number = read_number;
	row_data->start_sample = start_sample;
	row_data->median_before = median_before;
	row_data->channel = channel;
	row_data->well = well;
	row_data->pore_type = pore_type;
	row_data->calibration_offset = calibration_offset;
	row_data->calibration_scale = calibration_scale;
	row_data->end_reason = end_reason;
	row_data->end_reason_forced = end_reason_forced;
	row_data->run_info_id = run_info_id;
	row_data->num_minknow_events = num_minknow_events;
	row_data->tracked_scaling_scale = tracked_scaling_scale;
	row_data->tracked_scaling_shift = tracked_scaling_shift;
	row_data->predicted_scaling_scale = predicted_scaling_scale;
	row_data->predicted_scaling_shift = predicted_scaling_shift;
	row_data->num_reads_since_mux_change = num_reads_since_mux_change;
	row_data->time_since_mux_change = time_since_mux_change;

	// Actually copy the data
	for (size_t i = 0; i < record_count; i++)
	{
		memcpy(read_id, in_data[i].read_id, sizeof(read_id_t));
		read_id++;

		read_number[i] = in_data[i].read_number;
		start_sample[i] = in_data[i].start_sample;
		median_before[i] = in_data[i].median_before;
		channel[i] = in_data[i].channel;
		well[i] = in_data[i].well;
		char *pore_type_str;
		size_t pore_type_str_len;
		pod5_get_pore_wrapped(batch, in_data[i].pore_type, &pore_type_str, &pore_type_str_len);
		auto it = pore_type_cache->find(std::string(pore_type_str));
		if (it != pore_type_cache->end())
		{
			pore_type[i] = it->second;
		}
		else
		{
			int16_t new_pore_type;
			LOG_PROGRAM_ERROR(pod5_add_pore(&new_pore_type, writer, pore_type_str))
			pore_type_cache->insert({pore_type_str, new_pore_type});
			pore_type[i] = new_pore_type;
		}
		DEBUG_FREE(pore_type_str)
		//free(pore_type_str);

		calibration_offset[i] = in_data[i].calibration_offset;
		calibration_scale[i] = in_data[i].calibration_scale;
		assert((void*)read_id != (void*)end_reason);
		char *end_reason_str;
		size_t str_size;
		pod5_get_end_reason_wrapped(batch, in_data[i].end_reason, end_reason,
									&end_reason_str, &str_size);
		end_reason++;
		DEBUG_FREE(end_reason_str);

		end_reason_forced[i] = in_data[i].end_reason_forced;
		run_info_id[i] = in_data[i].run_info;

		num_minknow_events[i] = in_data[i].num_minknow_events;
		tracked_scaling_scale[i] = in_data[i].tracked_scaling_scale;
		tracked_scaling_shift[i] = in_data[i].tracked_scaling_shift;

		predicted_scaling_scale[i] = in_data[i].predicted_scaling_scale;
		predicted_scaling_shift[i] = in_data[i].predicted_scaling_shift;

		num_reads_since_mux_change[i] = in_data[i].num_reads_since_mux_change;
		time_since_mux_change[i] = in_data[i].time_since_mux_change;
	}
}

void free_batch_array(ReadBatchRowInfoArray_t *data)
{
	free((void *)data->read_id);
	free((void *)data->read_number);
	free((void *)data->start_sample);
	free((void *)data->median_before);
	free((void *)data->channel);
	free((void *)data->well);
	free((void *)data->pore_type);
	free((void *)data->calibration_offset);
	free((void *)data->calibration_scale);
	free((void *)data->end_reason);
	free((void *)data->end_reason_forced);
	free((void *)data->run_info_id);
	free((void *)data->num_minknow_events);
	free((void *)data->tracked_scaling_scale);
	free((void *)data->tracked_scaling_shift);
	free((void *)data->predicted_scaling_scale);
	free((void *)data->predicted_scaling_shift);
	free((void *)data->num_reads_since_mux_change);
	free((void *)data->time_since_mux_change);
}

pod5_error_t pod5_add_run_info_wrapped(Pod5FileWriter_t *writer,
									   RunInfoDictData_t *run_info_struct)
{
	int16_t written_idx;
	return pod5_add_run_info(
		&written_idx, writer, run_info_struct->acquisition_id,
		run_info_struct->acquisition_start_time_ms, run_info_struct->adc_max,
		run_info_struct->adc_min, run_info_struct->context_tags.size,
		run_info_struct->context_tags.keys, run_info_struct->context_tags.values,
		run_info_struct->experiment_name, run_info_struct->flow_cell_id,
		run_info_struct->flow_cell_product_code, run_info_struct->protocol_name,
		run_info_struct->protocol_run_id, run_info_struct->protocol_start_time_ms,
		run_info_struct->sample_id, run_info_struct->sample_rate,
		run_info_struct->sequencing_kit, run_info_struct->sequencer_position,
		run_info_struct->sequencer_position_type, run_info_struct->software,
		run_info_struct->system_name, run_info_struct->system_type,
		run_info_struct->tracking_id.size, run_info_struct->tracking_id.keys,
		run_info_struct->tracking_id.values);
}

int main(int argc, char **argv)
{

	char *curr_dir = alloc_and_get_curr_dir_name();

	pore_type_cache = std::make_unique<std::map<std::string, int16_t>>();
	if (pore_type_cache == nullptr)
	{
		EXIT_PROGRAM_ON_FAIL(nullptr, nullptr)
	}
	
	if (argc > 4)
	{
		std::cerr << "Ignoring extra arguments (only first 3 considered)\n";
	}
	if (argc == 1)
	{
		std::cerr << "No input file specified\n";
		return EXIT_FAILURE;
	}
	if (argc == 2)
	{
		std::cerr << "No output file specified\n";
		return EXIT_FAILURE;
	}
	CompressionOption comp_opt = DEFAULT_SIGNAL_COMPRESSION;
	if (argc == 4)
	{
		if (strncmp(argv[3], "--VBZ", 5) == 0)
		{
			comp_opt = VBZ_SIGNAL_COMPRESSION;
		}
		else if (strncmp(argv[3], "--uncompressed", 14) == 0)
		{
			comp_opt = UNCOMPRESSED_SIGNAL;
		}
		else
		{
			std::cerr << "Incorrect compression method";
			exit(-1);
		}
	}

	std::string in_filename = argv[1];
	std::string out_filename = argv[2];
	if (is_path_relative(in_filename))
		in_filename = std::string(curr_dir) + '/' + in_filename;
	if (is_path_relative(out_filename))
		out_filename = std::string(curr_dir) + '/' + out_filename;
	Pod5FileReader_t *reader;
	Pod5FileWriter_t *writer;
	const Pod5WriterOptions_t writer_options = {0, comp_opt, 0, 0};

	pod5_init();

	reader = pod5_open_file(in_filename.data());
	EXIT_PROGRAM_ON_FAIL(reader, writer)

	if (boost::filesystem::exists(boost::filesystem::path(out_filename.data())))
	{
		remove(out_filename.data());
	}

	writer = pod5_create_file(out_filename.data(), "Python API", &writer_options);
	EXIT_PROGRAM_ON_FAIL(reader, writer)

	size_t read_count;
	LOG_PROGRAM_ERROR(pod5_get_read_count(reader, &read_count))

	size_t batch_count;
	LOG_PROGRAM_ERROR(pod5_get_read_batch_count(&batch_count, reader))

	FileInfo_t file_info;
	LOG_PROGRAM_ERROR(pod5_get_file_info(reader, &file_info))

	for (size_t current_batch_idx = 0; current_batch_idx < batch_count; current_batch_idx++)
	{
		Pod5ReadRecordBatch_t *current_batch;
		LOG_PROGRAM_ERROR(pod5_get_read_batch(&current_batch, reader, current_batch_idx))
		size_t batch_row_count;
		LOG_PROGRAM_ERROR(pod5_get_read_batch_row_count(&batch_row_count, current_batch))
		ReadBatchRowInfo_t read_record_batch_array[batch_row_count]; // For large batches this could cause a stack overflow. Should push data to heap
		size_t sample_count[batch_row_count];
		int16_t *signal[batch_row_count];

		for (size_t current_batch_row = 0; current_batch_row < batch_row_count;
			 current_batch_row++)
		{
			// Load ReadBatchRowInfo to memory
			uint16_t read_table_version;
			LOG_PROGRAM_ERROR(pod5_get_read_batch_row_info_data(
				current_batch, current_batch_row, READ_BATCH_ROW_INFO_VERSION,
				&read_record_batch_array[current_batch_row], &read_table_version));

			LOG_PROGRAM_ERROR(pod5_get_read_complete_sample_count(
				reader, current_batch, current_batch_row,
				&sample_count[current_batch_row]))

			signal[current_batch_row] =
				(int16_t *)malloc(sizeof(int16_t) * sample_count[current_batch_row]);
			LOG_PROGRAM_ERROR(pod5_get_read_complete_signal(
				reader, current_batch, current_batch_row,
				sample_count[current_batch_row], signal[current_batch_row]))
		}

		uint32_t signal_length[batch_row_count];
		for (size_t i = 0; i < batch_row_count; i++)
		{
			signal_length[i] = sample_count[i];
		}

		static ReadBatchRowInfoArray_t flattened_array;
		transform_read_data_batch_array(read_record_batch_array, batch_row_count, current_batch, writer, &flattened_array);
		LOG_PROGRAM_ERROR(pod5_add_reads_data(
			writer, batch_row_count, READ_BATCH_ROW_INFO_VERSION, &flattened_array,
			const_cast<const int16_t **>(signal), signal_length))

		free_batch_array(&flattened_array);

		for (size_t i = 0; i < batch_row_count; i++)
		{
			free(signal[i]);
		}
		LOG_PROGRAM_ERROR(pod5_free_read_batch(current_batch))
	}

	run_info_index_t file_run_info_count;
	LOG_PROGRAM_ERROR(pod5_get_file_run_info_count(reader, &file_run_info_count))
	for (run_info_index_t i = 0; i < file_run_info_count; i++)
	{
		RunInfoDictData_t *run_info_struct;
		pod5_get_file_run_info(reader, i, &run_info_struct);
		LOG_PROGRAM_ERROR(pod5_add_run_info_wrapped(writer, run_info_struct))
		pod5_free_run_info(run_info_struct);
	}

	free(curr_dir);
	release_pod5_resources(reader, writer);
	return EXIT_SUCCESS;
}
