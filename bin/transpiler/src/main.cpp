#include <iostream>
#include <fstream>
#include <vector>
#include <random>

#ifndef BOOST_FILESYSTEM_NO_DEPRECATED
#define BOOST_FILESYSTEM_NO_DEPRECATED
#endif
#ifndef BOOST_SYSTEM_NO_DEPRECATED
#define BOOST_SYSTEM_NO_DEPRECATED
#endif

#include <boost/json/src.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/optional.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/test/unit_test.hpp>

#include <nil/crypto3/algebra/curves/pallas.hpp>
#include <nil/crypto3/algebra/fields/arithmetic_params/pallas.hpp>

#include <nil/crypto3/zk/snark/arithmetization/plonk/params.hpp>
#include <nil/crypto3/zk/snark/arithmetization/plonk/constraint_system.hpp>

#include <nil/marshalling/status_type.hpp>
#include <nil/marshalling/field_type.hpp>
#include <nil/marshalling/endianness.hpp>
#include <nil/crypto3/marshalling/zk/types/placeholder/proof.hpp>
#include <nil/crypto3/marshalling/zk/types/plonk/constraint_system.hpp>
#include <nil/crypto3/marshalling/zk/types/plonk/assignment_table.hpp>

#include <nil/crypto3/zk/snark/systems/plonk/placeholder/preprocessor.hpp>
#include <nil/crypto3/zk/snark/systems/plonk/placeholder/prover.hpp>
#include <nil/crypto3/zk/snark/systems/plonk/placeholder/verifier.hpp>
#include <nil/crypto3/zk/snark/systems/plonk/placeholder/params.hpp>
#include <nil/crypto3/zk/snark/systems/plonk/placeholder/profiling.hpp>

#include <nil/crypto3/math/polynomial/polynomial.hpp>
#include <nil/crypto3/math/algorithms/calculate_domain_set.hpp>

#include <nil/blueprint/asserts.hpp>
#include <nil/blueprint/transpiler/minimized_profiling_plonk_circuit.hpp>
#include <nil/blueprint/transpiler/evm_verifier_gen.hpp>
#include <nil/blueprint/transpiler/public_input.hpp>

template<typename ProfilingType, typename ConstraintSystemType, typename ColumnsRotationsType,
         typename ArithmetizationParams>
void print_sol_files(ConstraintSystemType &constraint_system, ColumnsRotationsType &columns_rotations,
                     std::string out_folder_path = ".", bool optimize_gates = false) {
    ProfilingType::process_split(
        nil::blueprint::main_sol_file_template,
        nil::blueprint::gate_sol_file_template,
        constraint_system,
        columns_rotations,
        out_folder_path,
        optimize_gates
    );
}

inline std::vector<std::size_t> generate_random_step_list(const std::size_t r, const int max_step) {
    using dist_type = std::uniform_int_distribution<int>;
    static std::random_device random_engine;

    std::vector<std::size_t> step_list;
    std::size_t steps_sum = 0;
    while (steps_sum != r) {
        if (r - steps_sum <= max_step) {
            while (r - steps_sum != 1) {
                step_list.emplace_back(r - steps_sum - 1);
                steps_sum += step_list.back();
            }
            step_list.emplace_back(1);
            steps_sum += step_list.back();
        } else {
            step_list.emplace_back(dist_type(1, max_step)(random_engine));
            steps_sum += step_list.back();
        }
    }
    return step_list;
}

template<typename FRIScheme, typename FieldType>
typename FRIScheme::params_type create_fri_params(
        std::size_t degree_log, const int max_step = 1, std::size_t expand_factor = 2) {
    std::size_t r = degree_log - 1;

    return typename FRIScheme::params_type(
        (1 << degree_log) - 1, // max_degree
        nil::crypto3::math::calculate_domain_set<FieldType>(degree_log + expand_factor, r),
        generate_random_step_list(r, max_step),
        expand_factor
    );
}

template<typename TIter>
void print_hex_byteblob(std::ostream &os, TIter iter_begin, TIter iter_end, bool endl) {
    os << "0x" << std::hex;
    for (TIter it = iter_begin; it != iter_end; it++) {
        os << std::setfill('0') << std::setw(2) << std::right << int(*it);
    }
    os << std::dec;
    if (endl) {
        os << std::endl;
    }
}

template<typename Endianness, typename Proof, typename CommitmentParamsType>
void proof_print(Proof &proof, const CommitmentParamsType& commitment_params, const std::string &output_file) {
    using namespace nil::crypto3::marshalling;

    using TTypeBase = nil::marshalling::field_type<Endianness>;
    using proof_marshalling_type = nil::crypto3::zk::snark::placeholder_proof<TTypeBase, Proof>;
    auto filled_placeholder_proof = types::fill_placeholder_proof<Endianness, Proof>(proof, commitment_params);

    std::vector<std::uint8_t> cv;
    cv.resize(filled_placeholder_proof.length(), 0x00);
    auto write_iter = cv.begin();
    nil::marshalling::status_type status = filled_placeholder_proof.write(write_iter, cv.size());
    std::ofstream out;
    out.open(output_file);
    print_hex_byteblob(out, cv.cbegin(), cv.cend(), false);
}

struct ParametersPolicy {
    constexpr static const std::size_t WitnessColumns = WITNESS_COLUMNS;
    constexpr static const std::size_t PublicInputColumns = PUBLIC_INPUT_COLUMNS;
    constexpr static const std::size_t ComponentConstantColumns = COMPONENT_CONSTANT_COLUMNS;
    constexpr static const std::size_t LookupConstantColumns = LOOKUP_CONSTANT_COLUMNS;
    constexpr static const std::size_t ComponentSelectorColumns = COMPONENT_SELECTOR_COLUMNS;
    constexpr static const std::size_t LookupSelectorColumns = LOOKUP_SELECTOR_COLUMNS;
    constexpr static const std::size_t lambda = LAMBDA;
    constexpr static const std::size_t GrindingBits = GRINDING_BITS;
    constexpr static const bool UseGrinding = GrindingBits != 0;
private:
    using default_hash = nil::crypto3::hashes::keccak_1600<256>;
public:
    using hash =default_hash;
};

template<typename BlueprintFieldType, bool multiprover>
int curve_dependent_main(
    boost::program_options::options_description options_desc,
    boost::program_options::variables_map vm
);

int main(int argc, char *argv[]) {

    boost::program_options::options_description options_desc("zkLLVM circuit EVM gate argument transpiler");

    // clang-format off
    options_desc.add_options()("help,h", "Display help message")
            ("version,v", "Display version")
            ("mode,m", boost::program_options::value<std::string>(), "Transpiler mode (gen-circuit-params, gen-gate-argument, gen-test-proof, gen-evm-verifier).\
            gen-gate-argument prepares gate argument and some placeholder params.\
            gen-circuit-params prepares circuit parameters for verification.\
            gen-test-proof prepares gate argument, placeholder params and sample proof for local testing.\
            gen-evm-verifier generates all modules for evm verification.")
            ("public-input,i", boost::program_options::value<std::string>(), "Public input file")
            ("assignment-table,t", boost::program_options::value<std::string>(), "Assignment table input file")
            ("circuit,c", boost::program_options::value<std::string>(), "Circuit input file")
            ("output-folder-path,o", boost::program_options::value<std::string>(), "Output folder absolute path.\
            It'll be better to create an empty folder for output")
            ("optimize-gates", "Put multiple small gates into several .sol file, sets reasonable threshold limits")
            ("skip-verification", "Used with gen-test-proof, if set - skips verifiyng the generated proof")
            ("gates-contract-size-threshold", boost::program_options::value<std::size_t>(), "Gates library size limit, per module. Default = 0, each constraint in a separate library")
            ("lookups-library-threshold", boost::program_options::value<std::size_t>(), "Lookups library size limit, per module. Default = 0, each lookup in a separate library")
            ("lookups-inline-threshold", boost::program_options::value<std::size_t>(), "Lookups inline size limit. Default = 0, none of the lookups are inlined")
            ("deduce-horner", "Detect polynomials over one variable and deduce to Horner's formula")
            ("optimize-powers", "Optimize terms that are powers of single variable")
            ("multi-prover", "Pass this flag if input circuit is a part of larger circuit, divided for faster paralel proving")
            ("elliptic-curve-type,e", boost::program_options::value<std::string>(), "Native elliptic curve type (pallas, vesta, ed25519, bls12381)")
            ;
    // clang-format on

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::command_line_parser(argc, argv).options(options_desc).run(),
                                  vm);
    boost::program_options::notify(vm);

    if (vm.count("help")) {
        std::cout << options_desc << std::endl;
        return 0;
    }

    if (vm.count("version")) {
#ifdef TRANSPILER_VERSION
#define xstr(s) str(s)
#define str(s) #s
        std::cout << xstr(TRANSPILER_VERSION) << std::endl;
#else
        std::cout << "Version is not defined" << std::endl;
#endif
        return 0;
    }

    std::string elliptic_curve;

    if (vm.count("elliptic-curve-type")) {
        elliptic_curve = vm["elliptic-curve-type"].as<std::string>();
    } else {
        std::cerr << "Invalid command line argument - elliptic curve type is not specified" << std::endl;
        std::cout << options_desc << std::endl;
        return 1;
    }

    std::map<std::string, int> curve_options{
        {"pallas", 0},
        {"vesta", 1},
        {"ed25519", 2},
        {"bls12381", 3},
    };

    if (curve_options.find(elliptic_curve) == curve_options.end()) {
        std::cerr << "Invalid command line argument -e (Native elliptic curve type): " << elliptic_curve << std::endl;
        std::cout << options_desc << std::endl;
        return 1;
    }

    switch (curve_options[elliptic_curve]) {
        case 0: {
            using curve_type = nil::crypto3::algebra::curves::pallas;
            using BlueprintFieldType = typename curve_type::base_field_type;
            return (vm.count("multi-prover") > 0) ?
                curve_dependent_main<BlueprintFieldType, true>(options_desc, vm) :
                curve_dependent_main<BlueprintFieldType, false>(options_desc, vm);
            break;
        }
        case 1: {
            UNREACHABLE("vesta curve based circuits are not supported yet");
            break;
        }
        case 2: {
            UNREACHABLE("ed25519 curve based circuits are not supported yet");
            break;
        }
        case 3: {
            using curve_type = nil::crypto3::algebra::curves::bls12<381>;
            using BlueprintFieldType = typename curve_type::base_field_type;
            return (vm.count("multi-prover") > 0) ?
                curve_dependent_main<BlueprintFieldType, true>(options_desc, vm) :
                curve_dependent_main<BlueprintFieldType, false>(options_desc, vm);
            break;
        }
    };

}

template<typename BlueprintFieldType, bool is_multi_prover>
int curve_dependent_main(
    boost::program_options::options_description options_desc,
    boost::program_options::variables_map vm
) {

    std::string mode;
    std::string assignment_table_file_name;
    std::string circuit_file_name;
    std::string output_folder_path;
    std::string public_input;

    if (vm.count("mode")) {
        mode = vm["mode"].as<std::string>();
    } else {
        std::cerr << "Invalid mode specified" << std::endl;
        std::cout << options_desc << std::endl;
        return 1;
    }

    if (!(mode == "gen-test-proof" || mode == "gen-gate-argument" || mode == "gen-circuit-params" || mode == "gen-evm-verifier")) {
        std::cerr << "Invalid mode specified" << std::endl;
        std::cout << options_desc << std::endl;
        return 1;
    }

    if (vm.count("assignment-table")) {
        assignment_table_file_name = vm["assignment-table"].as<std::string>();
    } else {
        std::cerr << "Invalid command line argument - assignment table file name is not specified" << std::endl;
        std::cout << options_desc << std::endl;
        return 1;
    }

    if (vm.count("circuit")) {
        circuit_file_name = vm["circuit"].as<std::string>();
    } else {
        std::cerr << "Invalid command line argument - circuit file name is not specified" << std::endl;
        std::cout << options_desc << std::endl;
        return 1;
    }

    if (vm.count("output-folder-path")) {
        output_folder_path = vm["output-folder-path"].as<std::string>();
        boost::filesystem::path dir(output_folder_path);
        if(boost::filesystem::create_directory(output_folder_path))
        {
            std::cerr<< "Directory Created: "<<output_folder_path<<std::endl;
        }
    } else {
        std::cerr << "Invalid command line argument - output folder path is not specified" << std::endl;
        std::cout << options_desc << std::endl;
        return 1;
    }

    constexpr std::size_t WitnessColumns = ParametersPolicy::WitnessColumns;
    constexpr std::size_t PublicInputColumns = is_multi_prover ? ParametersPolicy::PublicInputColumns + 1 : ParametersPolicy::PublicInputColumns;
    constexpr std::size_t ConstantColumns = ParametersPolicy::ComponentConstantColumns + ParametersPolicy::LookupConstantColumns;;
    constexpr std::size_t SelectorColumns = ParametersPolicy::ComponentSelectorColumns + ParametersPolicy::LookupSelectorColumns;

    using ArithmetizationParams =
        nil::crypto3::zk::snark::plonk_arithmetization_params<WitnessColumns, PublicInputColumns, ConstantColumns,
                                                              SelectorColumns>;
    using ConstraintSystemType =
        nil::crypto3::zk::snark::plonk_constraint_system<BlueprintFieldType, ArithmetizationParams>;
    using TableDescriptionType =
        nil::crypto3::zk::snark::plonk_table_description<BlueprintFieldType, ArithmetizationParams>;
    using Endianness = nil::marshalling::option::big_endian;
    using TTypeBase = nil::marshalling::field_type<Endianness>;
    using value_marshalling_type =
        nil::crypto3::marshalling::types::plonk_constraint_system<TTypeBase, ConstraintSystemType>;

    using ColumnType = nil::crypto3::zk::snark::plonk_column<BlueprintFieldType>;
    using AssignmentTableType =
        nil::crypto3::zk::snark::plonk_table<BlueprintFieldType, ArithmetizationParams, ColumnType>;
    using table_value_marshalling_type =
        nil::crypto3::marshalling::types::plonk_assignment_table<TTypeBase, AssignmentTableType>;

    using ColumnsRotationsType = std::array<std::set<int>, ArithmetizationParams::total_columns>;
    using ProfilingType = nil::blueprint::minimized_profiling_plonk_circuit<BlueprintFieldType, ArithmetizationParams>;

    if( vm.count("public-input") ){
        public_input = vm["public-input"].as<std::string>();
        if( !boost::filesystem::exists(public_input) ){
            std::cerr << "Invalid command line argument - public input file does not exist" << std::endl;
            return 1;
        }
        boost::filesystem::copy(public_input, output_folder_path+"/public_input.json", boost::filesystem::copy_options::overwrite_existing);
    }

    ConstraintSystemType constraint_system;
    {
        std::ifstream ifile;
        ifile.open(circuit_file_name, std::ios_base::binary | std::ios_base::in);
        if (!ifile.is_open()) {
            std::cout << "Cannot find input file " << circuit_file_name << std::endl;
            return 1;
        }
        std::vector<std::uint8_t> v;
        ifile.seekg(0, std::ios_base::end);
        const auto fsize = ifile.tellg();
        v.resize(fsize);
        ifile.seekg(0, std::ios_base::beg);
        ifile.read(reinterpret_cast<char*>(v.data()), fsize);
        if (!ifile) {
            std::cout << "Cannot parse input file " << circuit_file_name << std::endl;
            return 1;
        }
        ifile.close();

        value_marshalling_type marshalled_data;
        auto read_iter = v.begin();
        auto status = marshalled_data.read(read_iter, v.size());
        constraint_system = nil::crypto3::marshalling::types::make_plonk_constraint_system<Endianness, ConstraintSystemType>(
                marshalled_data
        );
    }

    TableDescriptionType table_description;
    AssignmentTableType assignment_table;
    {
        std::ifstream iassignment;
        iassignment.open(assignment_table_file_name, std::ios_base::binary | std::ios_base::in);
        if (!iassignment) {
            std::cout << "Cannot open " << assignment_table_file_name << std::endl;
            return 1;
        }
        std::vector<std::uint8_t> v;
        iassignment.seekg(0, std::ios_base::end);
        const auto fsize = iassignment.tellg();
        v.resize(fsize);
        iassignment.seekg(0, std::ios_base::beg);
        iassignment.read(reinterpret_cast<char*>(v.data()), fsize);
        if (!iassignment) {
            std::cout << "Cannot parse input file " << assignment_table_file_name << std::endl;
            return 1;
        }
        iassignment.close();
        table_value_marshalling_type marshalled_table_data;
        auto read_iter = v.begin();
        auto status = marshalled_table_data.read(read_iter, v.size());
        std::tie(table_description.usable_rows_amount, assignment_table) =
            nil::crypto3::marshalling::types::make_assignment_table<Endianness, AssignmentTableType>(
                marshalled_table_data
            );
        table_description.rows_amount = assignment_table.rows_amount();
    }

    auto columns_rotations = ProfilingType::columns_rotations(constraint_system, table_description);

    const std::size_t Lambda = 9;//ParametersPolicy::lambda;
    using Hash = nil::crypto3::hashes::keccak_1600<256>;
    using circuit_params = nil::crypto3::zk::snark::placeholder_circuit_params<
        BlueprintFieldType, ArithmetizationParams
    >;

    std::size_t table_rows_log = std::ceil(std::log2(table_description.rows_amount));

    using lpc_params_type = nil::crypto3::zk::commitments::list_polynomial_commitment_params<
        Hash,
        Hash,
        Lambda,
        2,
        ParametersPolicy::UseGrinding,
        nil::crypto3::zk::commitments::proof_of_work<Hash, std::uint32_t, ParametersPolicy::GrindingBits>
    >;
    using lpc_type = nil::crypto3::zk::commitments::list_polynomial_commitment<BlueprintFieldType, lpc_params_type>;
    using lpc_scheme_type = typename nil::crypto3::zk::commitments::lpc_commitment_scheme<lpc_type>;
    using placeholder_params = nil::crypto3::zk::snark::placeholder_params<circuit_params, lpc_scheme_type>;
    using policy_type = nil::crypto3::zk::snark::detail::placeholder_policy<BlueprintFieldType, placeholder_params>;

    auto fri_params = create_fri_params<typename lpc_type::fri_type, BlueprintFieldType>(table_rows_log);
    std::size_t permutation_size =
        table_description.witness_columns + table_description.public_input_columns + ParametersPolicy::ComponentConstantColumns;
    lpc_scheme_type lpc_scheme(fri_params);


    if (mode == "gen-gate-argument") {
        bool optimize_gates = false;
        if( vm.count("optimize-gates") )
            optimize_gates = true;
        print_sol_files<ProfilingType, ConstraintSystemType, ColumnsRotationsType, ArithmetizationParams>(
            constraint_system, columns_rotations, output_folder_path, optimize_gates);
    }

    if ((mode != "gen-circuit-params") && (mode != "gen-test-proof") && (mode != "gen-evm-verifier")) {
        return 0;
    }

    std::cout << "Preprocessing public data..." << std::endl;
    typename nil::crypto3::zk::snark::placeholder_public_preprocessor<
        BlueprintFieldType, placeholder_params>::preprocessed_data_type public_preprocessed_data =
    nil::crypto3::zk::snark::placeholder_public_preprocessor<BlueprintFieldType, placeholder_params>::process(
        constraint_system, assignment_table.public_table(), table_description, lpc_scheme, permutation_size);

    if (mode == "gen-evm-verifier") {
        std::size_t gates_contract_size_threshold = 800;
        std::size_t lookups_library_threshold = 0;
        std::size_t lookups_inline_threshold = 0;
        bool deduce_horner = false;
        bool optimize_powers = false;

        if ( vm.count("optimize-gates") >0 ) {
            lookups_library_threshold = 1000;
            lookups_inline_threshold = 1000;
            deduce_horner = true;
            optimize_powers = true;
        }

        if ( vm.count("gates-contract-size-threshold") ) {
            gates_contract_size_threshold = vm["gates-contract-size-threshold"].as<std::size_t>();
        }
        if ( vm.count("lookups-library-threshold") ) {
            lookups_library_threshold = vm["lookups-library-threshold"].as<std::size_t>();
        }
        if ( vm.count("lookups-inline-threshold") ) {
            lookups_inline_threshold = vm["lookups-inline-threshold"].as<std::size_t>();
        }
        if ( vm.count("deduce-horner") > 0 ) {
            deduce_horner = true;
        }
        if ( vm.count("optimize-powers") > 0 ) {
            optimize_powers = true;
        }
        nil::blueprint::evm_verifier_printer<placeholder_params>(
            constraint_system,
            public_preprocessed_data.common_data,
            lpc_scheme,
            permutation_size,
            output_folder_path,
            gates_contract_size_threshold,
            lookups_library_threshold,
            lookups_inline_threshold,
            deduce_horner,
            optimize_powers
        ).print();
        return 0;
    }

    nil::crypto3::zk::snark::print_placeholder_params<placeholder_params>(
        public_preprocessed_data, lpc_scheme, output_folder_path+"/circuit_params.json");

    if (mode == "gen-test-proof") {
        std::cout << "Preprocessing private data..." << std::endl;
        typename nil::crypto3::zk::snark::placeholder_private_preprocessor<
            BlueprintFieldType, placeholder_params>::preprocessed_data_type private_preprocessed_data =
            nil::crypto3::zk::snark::placeholder_private_preprocessor<BlueprintFieldType, placeholder_params>::process(
                constraint_system, assignment_table.private_table(), table_description
            );

        if (constraint_system.num_gates() == 0){
            std::cout << "Generating proof..." << std::endl;
            std::cout << "Proof generated" << std::endl;
            if( !vm.count("skip-verification") ) {
                std::cout << "Verifying proof..." << std::endl;
                std::cout << "Proof is verified" << std::endl;
            } else {
                std::cout << "Proof verification skipped" << std::endl;
            }
            std::string proof_path = output_folder_path + "/proof.bin";
            std::cout << "Writing proof to " << proof_path << "..." << std::endl;
            std::fstream fs;
            fs.open(proof_path, std::ios::out);
            fs.close();
            std::cout << "Proof written" << std::endl;
        } else {
            std::cout << "Generating proof..." << std::endl;
            using ProofType = nil::crypto3::zk::snark::placeholder_proof<BlueprintFieldType, placeholder_params>;
            ProofType proof = nil::crypto3::zk::snark::placeholder_prover<BlueprintFieldType, placeholder_params>::process(
                public_preprocessed_data, private_preprocessed_data, table_description, constraint_system, lpc_scheme
            );
            std::cout << "Proof generated" << std::endl;

            if( !vm.count("skip-verification") ) {
                std::cout << "Verifying proof..." << std::endl;
                bool verification_result =
                    nil::crypto3::zk::snark::placeholder_verifier<BlueprintFieldType, placeholder_params>::process(
                        public_preprocessed_data, proof, constraint_system, lpc_scheme
                    );

                ASSERT_MSG(verification_result, "Proof is not verified" );
                std::cout << "Proof is verified" << std::endl;
            }

            std::string proof_path = output_folder_path + "/proof.bin";
            std::cout << "Writing proof to " << proof_path << "..." << std::endl;
            proof_print<Endianness, ProofType>(proof, fri_params, proof_path);
            std::cout << "Proof written" << std::endl;
        }
        return 0;
    }
}
