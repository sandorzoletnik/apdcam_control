#include <cctype>
#include <map>
#include <iostream>

namespace apdcam10g
{
    class settings
    {
    private:
        std::map<std::string,std::string> data_;

    public:

        // An intermediate class to access values by key in the settings class. This class only stores references.
        // This class can be assigned, and if the given key does not exist in settings, it is created.
        // Upon reading, however (by the conversion operators to int, bool, string, etc) an exception is thrown
        // if the key does not exist
        class value_ref
        {
        private:
            settings *settings_ = 0;
            std::string key_ = "";

            static std::string tolower(const std::string &s) 
                {
                    std::string result = "";
                    for(auto c : s) result += std::tolower(c);
                    return result;
                }

        public:
            value_ref(settings *s, const std::string &k) : settings_(s), key_(k) {}

            bool exists() const { return settings_->data_.find(key_) != settings_->data_.end(); }

            template <typename T>
            void operator=(const T &val)
            {
                settings_->data_[key_] = std::to_string(val);
            }

            void operator=(const char *val)
            {
                settings_->data_[key_] = val;
            }

            operator unsigned int() const
            {
                if(!exists()) APDCAM_ERROR("This variable '" + key_ + "' does not exist");
                return std::stoi(settings_->data_[key_]);
            }

            operator int() const 
            {
                if(!exists()) APDCAM_ERROR("This variable '" + key_ + "' does not exist");
                return std::stoi(settings_->data_[key_]);
            }
            operator bool() const 
            {
                if(!exists()) APDCAM_ERROR("This variable '" + key_ + "' does not exist");
                std::string val = settings_->data_[key_];
                if(tolower(val) == "true"  || tolower(val) == "yes") return true;
                if(tolower(val) == "false" || tolower(val) == "no") return false;
                return std::stoi(settings_->data_[key_]);
            }
            operator double() const 
            {
                if(!exists()) APDCAM_ERROR("This variable '" + key_ + "' does not exist");
                return std::stof(settings_->data_[key_]);
            }
            operator std::string() const 
            {
                if(!exists()) APDCAM_ERROR("This variable '" + key_ + "' does not exist");
                return settings_->data_[key_];
            }
        };

        friend class value_ref;

        value_ref operator[](const std::string &key)
        {
            return value_ref(this,key);
        }

        friend std::ostream &operator<<(std::ostream &, const settings &);
        friend std::istream &operator>>(std::istream &,  settings &);
    };

    inline std::ostream &operator<<(std::ostream &out, const settings &s)
    {
        for(auto i = s.data_.begin(); i != s.data_.end(); ++i)
        {
            out<<(*i).first<<"="<<(*i).second<<std::endl;
        }
        return out;
    }

    inline std::istream &operator>>(std::istream &in, settings &s)
    {
        s.data_.clear();
        std::string line;
        while(std::getline(in,line))
        {
            if(line.size() == 0) continue;
            auto pos = line.find('=');
            if(pos == std::string::npos) APDCAM_ERROR("Bad line in the settings file: " + line);
            std::string key = line.substr(0,pos);
            std::string val = line.substr(pos+1);
            s.data_[key] = val;
        }
        return in;
    }

}
