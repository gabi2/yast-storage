#!/usr/bin/env ruby
#
# encoding: utf-8

# Copyright (c) [2015] SUSE LLC
#
# All Rights Reserved.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of version 2 of the GNU General Public License as published
# by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, contact SUSE LLC.
#
# To contact SUSE LLC about this file by physical or electronic mail, you may
# find current contact information at www.suse.com.

require "yast"
require "storage"
require_relative "./disk_size"
require_relative "./storage_manager"
require_relative "./proposal_settings"
require_relative "./proposal_volume"
require_relative "./boot_requirements_checker"
require_relative "./disk_analyzer"
require_relative "./space_maker"
require "pp"

# This file can be invoked separately for minimal testing.
# Use 'sudo' if you do that since it will do hardware probing with libstorage.

module Yast
  module Storage
    #
    # Storage proposal for installation: Class that can suggest how to create
    # or change partitions for a Linux system installation based on available
    # storage devices (disks) and certain configuration parameters.
    #
    class Proposal
      include Yast::Logger

      attr_accessor :settings

      # devicegraph names
      PROPOSAL_BASE = "proposal_base"
      PROPOSAL      = "proposal"
      PROBED        = "probed"
      STAGING       = "staging"

      def initialize
        @storage  = nil
        @settings = ProposalSettings.new
        @proposal = nil # ::Storage::DeviceGraph
        @disk_blacklist = []
        @disk_greylist  = []
        @actions = nil
      end

      # Create a storage proposal.
      def propose
        prepare_devicegraphs

        boot_requirements_checker = BootRequirementsChecker.new(@settings)
        @volumes = boot_requirements_checker.needed_partitions
        @volumes += standard_volumes

        disk_analyzer = DiskAnalyzer.new
        disk_analyzer.analyze

        space_maker = SpaceMaker.new(@volumes, @settings, disk_analyzer)
        space_maker.find_space
      end

      def proposal_text
        # TO DO
        "No disks found - no storage proposal possible"
      end

      # Reset the proposal devicegraph (PROPOSAL) to PROPOSAL_BASE.
      #
      def reset_proposal
        storage = StorageManager.instance
        storage.remove_devicegraph(PROPOSAL) if storage.exist_devicegraph(PROPOSAL)
        @proposal = storage.copy_devicegraph(PROPOSAL_BASE, PROPOSAL)
        @actions  = nil
      end

      # Copy the PROPOSAL device graph to STAGING so actions can be calculated
      # or commited
      #
      def proposal_to_staging
        StorageManager.instance.copy_devicegraph(PROPOSAL, STAGING)
        @actions = nil
      end

      # Calculate action graph
      #
      def calc_actions
        @actions = StorageManager.instance.calculate_actiongraph unless @actions
      end


      private

      # Prepare the devicegraphs we are working on:
      #
      # - PROBED. This contains the disks and partitions that were probed.
      #
      # - PROPOSAL_BASE. This starts as a copy of PROBED. If the user decides
      #        in the UI to have some partitions removed or everything on a
      #        disk deleted to make room for the Linux installation, those
      #        partitions are already deleted here. This is the base for all
      #        calculated proposals. If a proposal goes wrong and needs to be
      #        reset internally, it will be reset to this state.
      #
      #  - PROPOSAL. This is the working devicegraph for the proposal
      #        calculations. If anything goes wrong, this might be reset (with
      #        reset_proposal) to PROPOSAL_BASE at any time.
      #
      # If no PROPOSAL_BASE devicegraph exists yet, it will be copied from PROBED.
      #
      def prepare_devicegraphs
        storage = StorageManager.instance # this will start probing in the first invocation
        storage.copy_devicegraph(PROBED, PROPOSAL_BASE) unless storage.exist_devicegraph(PROPOSAL_BASE)
        reset_proposal
      end

      # Return an array of the standard volumes for the root and /home file
      # systems
      #
      # @return [Array [ProposalVolume]]
      #
      def standard_volumes
        volumes = [make_root_vol]
        volumes << make_home_vol if @settings.use_separate_home
        volumes
      end

      # Create the ProposalVolume data structure for the root volume according
      # to the settings.
      #
      # This does NOT create the partition yet, only the data structure.
      #
      def make_root_vol
        root_vol = ProposalVolume.new("/", @settings.root_filesystem_type)
        root_vol.min_size = @settings.root_base_size
        root_vol.max_size = @settings.root_max_size
        if root_vol.filesystem_type = ::Storage::BTRFS
          multiplicator = 1.0 + @settings.btrfs_increase_percentage / 100.0
          root_vol.min_size *= multiplicator
          root_vol.max_size *= multiplicator
        end
        root_vol.desired_size = root_vol.max_size
        root_vol
      end

      # Create the ProposalVolume data structure for the /home volume according
      # to the settings.
      #
      # This does NOT create the partition yet, only the data structure.
      #
      def make_home_vol
        home_vol = ProposalVolume.new("/home", @settings.home_filesystem_type)
        home_vol.min_size = @settings.home_min_size
        home_vol.max_size = @settings.home_max_size
        home_vol.desired_size = home_vol.max_size
        home_vol
      end
    end
  end
end

# if used standalone, do a minimalistic test case

if $PROGRAM_NAME == __FILE__  # Called direcly as standalone command? (not via rspec or require)
  proposal = Yast::Storage::Proposal.new
  proposal.settings.root_filesystem_type = ::Storage::XFS
  proposal.settings.use_separate_home = true
  proposal.propose
  # pp proposal
end
