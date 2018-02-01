#include <vector>

#include "caffe/layers/conv_norm_layer.hpp"

namespace caffe {

template <typename Dtype>
void ConvolutionNormLayer<Dtype>::compute_output_shape() {
  const int* kernel_shape_data = this->kernel_shape_.cpu_data();
  const int* stride_data = this->stride_.cpu_data();
  const int* pad_data = this->pad_.cpu_data();
  const int* dilation_data = this->dilation_.cpu_data();
  this->output_shape_.clear();
  for (int i = 0; i < this->num_spatial_axes_; ++i) {
    // i + 1 to skip channel axis
    const int input_dim = this->input_shape(i + 1);
    const int kernel_extent = dilation_data[i] * (kernel_shape_data[i] - 1) + 1;
    const int output_dim = (input_dim + 2 * pad_data[i] - kernel_extent)
        / stride_data[i] + 1;
    this->output_shape_.push_back(output_dim);
  }
}

template <typename Dtype>
void ConvolutionNormLayer<Dtype>::Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
    BaseConvolutionLayer<Dtype>::Reshape(bottom,top);
    bottom_quantize_.Reshape(1,1,1,this->bottom_dim_);
    top_dequantize_.Reshape(1,1,1,this->top_dim_);
}

template <typename Dtype>
void ConvolutionNormLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  const Dtype* weight = this->blobs_[0]->cpu_data();
  for (int i = 0; i < bottom.size(); ++i) {
    const Dtype* bottom_data = bottom[i]->cpu_data();
    Dtype* top_data = top[i]->mutable_cpu_data();
    Dtype* bot_quan_data = bottom_quantize_.mutable_cpu_data();
    Dtype* top_dequan_data = top_dequantize_.mutable_cpu_data();
    for (int n = 0; n < this->num_; ++n) {
      // quantize to uint8
      for (int d=0; d<this->bottom_dim_; d++)
      {
          Dtype tmp = bottom_data[n*this->bottom_dim_+d];
          tmp = int(tmp*this->int_scale_+0.5f);
          tmp = tmp<0 ? 0 : tmp;
          tmp = tmp>255 ? 255:tmp;
          bot_quan_data[d] = tmp;
      }
      this->forward_cpu_gemm(bot_quan_data, weight,
                             top_dequan_data);
      // dequantize to float
      for (int d=0; d<this->top_dim_; d++)
      {
          Dtype tmp = top_dequan_data[d];
          top_data[n*this->top_dim_+d] = tmp/this->int_scale_;
      }
      if (this->bias_term_) {
        const Dtype* bias = this->blobs_[1]->cpu_data();
        this->forward_cpu_bias(top_data + n * this->top_dim_, bias);
      }
    }
  }
}

template <typename Dtype>
void ConvolutionNormLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {
  const Dtype* weight = this->blobs_[0]->cpu_data();
  Dtype* weight_diff = this->blobs_[0]->mutable_cpu_diff();
  for (int i = 0; i < top.size(); ++i) {
    const Dtype* top_diff = top[i]->cpu_diff();
    const Dtype* bottom_data = bottom[i]->cpu_data();
    Dtype* bottom_diff = bottom[i]->mutable_cpu_diff();
    Dtype* bot_quan_data = bottom_quantize_.mutable_cpu_data();

    // Bias gradient, if necessary.
    if (this->bias_term_ && this->param_propagate_down_[1]) {
      Dtype* bias_diff = this->blobs_[1]->mutable_cpu_diff();
      for (int n = 0; n < this->num_; ++n) {
        this->backward_cpu_bias(bias_diff, top_diff + n * this->top_dim_);
      }
    }
    if (this->param_propagate_down_[0] || propagate_down[i]) {
      for (int n = 0; n < this->num_; ++n) {
        // gradient w.r.t. weight. Note that we will accumulate diffs.
        if (this->param_propagate_down_[0]) {
            
          //  add quantize effect
          for (int d=0; d<this->bottom_dim_; d++)
          {
            Dtype tmp = bottom_data[n*this->bottom_dim_+d];
            tmp = int(tmp*this->int_scale_+0.5f);
            tmp = tmp<0 ? 0 : tmp;
            tmp = tmp>255 ? 255:tmp;
            bot_quan_data[d] = tmp/this->int_scale_;
          }
          this->weight_cpu_gemm(bot_quan_data, 
                                top_diff + n*this->top_dim_, weight_diff);
        }
        // gradient w.r.t. bottom data, if necessary.
        if (propagate_down[i]) {
          this->backward_cpu_gemm(top_diff + n * this->top_dim_, weight,
              bottom_diff + n * this->bottom_dim_);
        }
      }
    }
  }
}

#ifdef CPU_ONLY
STUB_GPU(ConvolutionNormLayer);
#endif

INSTANTIATE_CLASS(ConvolutionNormLayer);
REGISTER_LAYER_CLASS(ConvolutionNorm);
}  // namespace caffe