#!/usr/bin/env ruby

#-------------------------------------------------------------------------------
# This will graph the output from: hstress
#-------------------------------------------------
# Prereq: gem install gnuplot
#-------------------------------------------------------------------------------
# Copyright (c) 2012, William Zajac
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * The name of William Zajac may not be used to endorse or promote products
#       derived from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#-------------------------------------------------------------------------------

require 'optparse'
require 'ostruct'
require 'gnuplot'

class Plotter
    attr_reader :buckets, :bucket_symbols
    attr_accessor :font_file, :thumbnails, :font_size_thumb, :font_size_standard, :graph_size_thumb, :graph_size_standard, :data_file, :images_dir_name, :images_dir, :verbose

    def initialize()
        @buckets = [1,10,100]
        @bucket_symbols = @buckets.sort.map{|bucket| ("under_" + bucket.to_s).to_sym}
        @bucket_symbols << ("over_" + @buckets.sort.last.to_s).to_sym
        @font_file = "#{File.dirname(__FILE__)}/../fonts/Ropa_Sans/RopaSans-Regular.ttf"
        @font_size_thumb = "8"
        @font_size_standard = "13"
        @graph_size_thumb = "600,200"
        @graph_size_standard = "1200,300"
        @images_dir_name = "test_images"
        @thumbnails = false
        @verbose = false
    end

    # We need this to rebuild the bucket_symbols if they are reset
    def buckets=(value)
        @buckets = value
        @bucket_symbols = @buckets.sort.map{|bucket| ("under_" + bucket.to_s).to_sym}
    end

    def build_data_sets(data_file)
        graph_data_sets = Hash.new

        # Now that we know where the data file is, we can make the images directory relative to it
        @images_dir = File.dirname(data_file) + "/#{@images_dir_name}"
        puts "Images dir: #{@images_dir}"
        Dir.mkdir(@images_dir) unless File.exists?(@images_dir)

        f = File.new(data_file)
        f.each_line do |line|
            next unless line.match(/^\d{10}/)

            line_parts = line.split(/\t/)
            bucket_stack_total = 0
            conn_stack_total = 0
            http_stack_total = 0

            # Build out the initial arrays if they don't yet exist
            [:time,:conn_success,:conn_error,:conn_timeout,:conn_close,:http_success,:http_error,@bucket_symbols].flatten.each do |key|
                graph_data_sets.has_key?(key) || graph_data_sets[key] = Array.new
                value = line_parts.shift

                # Fix the time format
                if key == :time
                    t = Time.at(value.to_i)
                    value = t.hour.to_s + ":" + t.min.to_s + ":" + t.sec.to_s
                end

                # Create stacked graph data
                # Connection
                if key.to_s.match(/conn_/)
                    # stack the value
                    value = value.to_i
                    value += conn_stack_total.to_i

                    # update the bucket_stack total
                    conn_stack_total = value
                end
                # HTTP
                if key.to_s.match(/http_/)
                    # stack the value
                    value = value.to_i
                    value += http_stack_total.to_i

                    # update the bucket_stack total
                    http_stack_total = value
                end
                # Buckets
                if key.to_s.match(/under_|over_/)
                    # stack the value
                    value = value.to_i
                    value += bucket_stack_total.to_i

                    # update the bucket_stack total
                    bucket_stack_total = value
                end

                # Add the value
                graph_data_sets[key] << value
            end
        end
        f.close

        graph_data_sets
    end

    def make_graphs(title, graph_data_sets, graph_keys)
        x_axis = graph_data_sets[:time]

        # We will graph one resource_type per graph for now
        image_types = {".png"   => "png size #{@graph_size_standard} font \"#{@font_file},#{@font_size_standard}\""}
        image_types["_tn.png"] = "png size  #{@graph_size_thumb} font \"#{@font_file},#{@font_size_thumb}" if @thumbnails

        image_types.each do |image_extension, plot_terminal|
            Gnuplot.open do |gp|
                Gnuplot::Plot.new(gp) do |plot|
                    plot.title  title
                    plot.terminal  plot_terminal
                    png_file = "#{@images_dir}/#{title.sub(/\s.*/, "")}#{image_extension}"
                    # http://t16web.lanl.gov/Kawano/gnuplot/legend-e.html#2.2
                    plot.key  "below"
                    plot.sets << ("grid")
                    plot.output  png_file
                    plot.xdata "time"
                    plot.timefmt '"%H:%M:%S"'
                    plot.format 'x "%H:%M:%S"'

                    # Write out the name of the file being created
                    puts "  Generating Image: #{png_file}"

                    # Make a data set for each of the resources
                    graph_keys.each do |key|
                        y_axis = graph_data_sets[key]
                        label = key.to_s
                        if @verbose
                            puts "TITLE: " + title
                            puts "Key: " + label
                            puts "X_SIZE: " + x_axis.size.to_s
                            puts "Y_SIZE: " + y_axis.size.to_s
                            puts "X: " + x_axis.inspect
                            puts "Y: " + y_axis.inspect
                        end
                        plot.data << Gnuplot::DataSet.new( [x_axis, y_axis] ) do |ds|
                            # ds.notitle
                            ds.with = "filledcurves x1"
                            ds.title = label

                            # This allows it to quietly ignore missing values
                            ds.using = "1:2"
                        end
                    end
                end
            end
        end
    end
end



class OptParse
    def self.parse(args)
        options = OpenStruct.new
        options.verbose = false
        default_plotter_options = Plotter.new

        opts = OptionParser.new do |opts|
            opts.banner = "Graph output from hstress\n\nUsage: #{__FILE__} [options] -d DATA_FILE"

            opts.separator ""
            opts.separator "Required:"

            opts.on('-d', '--data_file DATA_FILE', "Data file of STDOUT from hstress") do |d|
                options.data_file = d
            end

            opts.separator ""
            opts.separator "Additional Options:"

            opts.on('-b', '--buckets BUCKET_LIST', "Buckets matching hstress (default: \"#{default_plotter_options.buckets.join(',')}\")") do |b|
                options.buckets = b.split(',').map{|s| s.to_i}
            end

            opts.on('-t', '--thumbnails', "Generate thumnail images also") do |thumbnails|
                options.thumbnails = thumbnails
            end

            opts.on('--images_dir IMAGES_DIR', "The directory in which to store the images (default: #{default_plotter_options.images_dir_name})") do |images_dir|
                options.images_dir_name = images_dir
            end

            opts.on('--font_file FONT_FILE', "Font file to use (default: \"#{default_plotter_options.font_file}\")") do |font_file|
                options.font_file = "#{File.dirname(__FILE__)}/{#font_file}"
            end

            opts.on('--font_size_thumb FONT_SIZE_THUMB', "Font size to use for thumbnails (default: #{default_plotter_options.font_size_thumb})") do |font_size_thumb|
                options.font_size_thumb = font_size_thumb
            end

            opts.on('--font_size_standard FONT_SIZE_STANDARD', "Font size to use for standard graphs (default: #{default_plotter_options.font_size_standard})") do |font_size_standard|
                options.font_size_standard = font_size_standard
            end

            opts.on('--graph_size_thumb GRAPH_SIZE_THUMB', "Size to use for thumb graphs (default: \"#{default_plotter_options.graph_size_thumb}\")") do |graph_size_thumb|
                options.graph_size_thumb = graph_size_thumb
            end

            opts.on('--graph_size_standard GRAPH_SIZE_STANDARD', "Size to use for standard graphs (default: \"#{default_plotter_options.graph_size_standard}\")") do |graph_size_standard|
                options.graph_size_standard = graph_size_standard
            end

            opts.separator ""

            opts.on("-v", "--verbose", "Show the values and other interesting info") do |v|
                options.verbose = v
            end

            opts.on_tail("-h", "--help", "Show this message") do
                puts opts
                exit
            end

        end

        opts.parse!(args)

        # Verify that the data_file was set and exists
        # raise OptionParser::MissingArgument if options.data_file.nil?
        if not options.data_file
            puts "\nERROR: Required argument -d DATA_FILE was not specified\n\n"
            puts opts
            exit(1)
        end

        if not File.exists?(options.data_file)
            puts "\nERROR: DATA_FILE: #{options.data_file} does not exist\n\n"
            puts opts
            exit(1)
        end

        options
    end
end


#-------------------------------------------------------------------------------
# Do the work
#-------------------------------------------------------------------------------
# Parse the opts
options = OptParse.parse(ARGV)

# Instantiate out plotter
plotter = Plotter.new

# Set the options
options.marshal_dump.each do |k,v| 
    if options.verbose
        puts "Setting: #{k} = #{v}"
    end
    # Override the default attrs with what was set in the command line args
    plotter.send "#{k}=".to_sym, v
end

# Build the data sets
graph_data_sets = plotter.build_data_sets(options.data_file)

# Make the following three graphs
# Order is important here for stacking
plotter.make_graphs("Connections", graph_data_sets, [:conn_timeout, :conn_error, :conn_success])
plotter.make_graphs("HTTP Status", graph_data_sets, [:http_error, :http_success])
plotter.make_graphs("Buckets (ms)", graph_data_sets, plotter.bucket_symbols.reverse)
